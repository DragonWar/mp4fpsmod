#include <vector>
#include <algorithm>
#include "mp4trackx.h"

using mp4v2::impl::MP4File;
using mp4v2::impl::MP4Track;
using mp4v2::impl::MP4Atom;

int gcd(int a, int b) { return !b ? a : gcd(b, a % b); }

int lcm(int a, int b) { return b * (a / gcd(a, b)); }

double fps2tsdelta(int num, int denom, int timeScale)
{
    return static_cast<double>(timeScale) / num * denom;
}

MP4TrackX::MP4TrackX(MP4File *pFile, MP4Atom *pTrackAtom)
    : MP4Track(pFile, pTrackAtom)
{
    FetchStts();
    FetchCtts();
    for (size_t i = 0; i < m_sampleTimes.size(); ++i)
	m_ctsIndex.push_back(i);
    std::sort(m_ctsIndex.begin(), m_ctsIndex.end(), CTSComparator(this));
}

void MP4TrackX::SetFPS(FPSRange *fpsRanges, size_t numRanges)
{
    uint32_t timeScale = CalcTimeScale(fpsRanges, fpsRanges + numRanges);
    uint64_t duration = CalcSampleTimes(
	fpsRanges, fpsRanges + numRanges, timeScale);
    DoEditTimeCodes(timeScale, duration);
}

void
MP4TrackX::SetTimeCodes(double *timeCodes, size_t count, uint32_t timeScale)
{
    if (count != GetNumberOfSamples())
	throw std::runtime_error(
		"timecode entry count differs from the movie");

    uint64_t ioffset = 0;
    for (size_t i = 0; i < count; ++i) {
	ioffset = static_cast<uint64_t>(timeCodes[i]);
	m_sampleTimes[i].dts = ioffset;
	m_sampleTimes[m_ctsIndex[i]].cts = ioffset;
    }
    DoEditTimeCodes(timeScale, ioffset);
}

void MP4TrackX::FetchStts()
{
    uint32_t numStts = m_pSttsCountProperty->GetValue();
    uint64_t dts = 0;
    for (uint32_t i = 0; i < numStts; ++i) {
	uint32_t sampleCount = m_pSttsSampleCountProperty->GetValue(i);
	uint32_t sampleDelta = m_pSttsSampleDeltaProperty->GetValue(i);
	for (uint32_t j = 0; j < sampleCount; ++j) {
	    SampleTime st = { dts, 0 };
	    m_sampleTimes.push_back(st);
	    dts += sampleDelta;
	}
    }
}

void MP4TrackX::FetchCtts()
{
    if (!m_pCttsCountProperty)
	return;
    {
	uint32_t numCtts = m_pCttsCountProperty->GetValue();
	SampleTime *sp = &m_sampleTimes[0];
	for (uint32_t i = 0; i < numCtts; ++i) {
	    uint32_t sampleCount = m_pCttsSampleCountProperty->GetValue(i);
	    uint32_t ctsOffset = m_pCttsSampleOffsetProperty->GetValue(i);
	    for (uint32_t j = 0; j < sampleCount; ++j) {
		sp->ctsOffset = ctsOffset;
		++sp;
	    }
	}
    }
    {
	SampleTime *sp = &m_sampleTimes[0];
	for (size_t i = 0; i < m_sampleTimes.size(); ++i) {
	    sp->cts = sp->dts + sp->ctsOffset;
	    ++sp;
	}
    }
}

uint32_t MP4TrackX::CalcTimeScale(FPSRange *begin, const FPSRange *end)
{
    uint32_t total = 0;
    FPSRange *fp;
    for (fp = begin; fp != end; ++fp) {
	if (fp->numFrames > 0)
	    total += fp->numFrames;
	else {
	    fp->numFrames = std::max(
		static_cast<int>(GetNumberOfSamples() - total), 0);
	    total += fp->numFrames;
	}
    }
    if (total != m_sampleTimes.size())
	throw std::runtime_error(
		"Total number of frames differs from the movie");

    int timeScale = 1;
    bool exact = true;
    for (fp = begin; fp != end; ++fp) {
	int g = gcd(fp->fps_num, fp->fps_denom);
	fp->fps_num /= g;
	fp->fps_denom /= g;
	timeScale = lcm(fp->fps_num, timeScale);
	if (timeScale == 0 || timeScale % fp->fps_num) {
	    // LCM overflowed
	    exact = false;
	    break;
	}
    }
    if (!exact) timeScale = 1000; // pick default value
    return timeScale;
}

uint64_t MP4TrackX::CalcSampleTimes(
	const FPSRange *begin, const FPSRange *end, uint32_t timeScale)
{
    double offset = 0.0;
    uint32_t frame = 0;
    for (const FPSRange *fp = begin; fp != end; ++fp) {
	double delta = fps2tsdelta(fp->fps_num, fp->fps_denom, timeScale);
	for (uint32_t i = 0; i < fp->numFrames; ++i) {
	    uint64_t ioffset = static_cast<uint64_t>(offset);
	    m_sampleTimes[frame].dts = ioffset;
	    m_sampleTimes[m_ctsIndex[frame]].cts = ioffset;
	    offset += delta; 
	    ++frame;
	}
    }
    return static_cast<uint64_t>(offset);
}

void MP4TrackX::DoEditTimeCodes(uint32_t timeScale, uint64_t duration)
{
    m_pTimeScaleProperty->SetValue(timeScale);
    m_pMediaDurationProperty->SetValue(0);
    UpdateDurations(duration);
    
    UpdateStts();
    if (m_pCttsCountProperty) {
	int64_t initialDelay = UpdateCtts();
	int64_t movieDuration = m_pTrackDurationProperty->GetValue();
	UpdateElst(movieDuration, initialDelay);
    }
    UpdateModificationTimes();
}

void MP4TrackX::UpdateStts()
{
    int32_t count = static_cast<int32_t>(m_pSttsCountProperty->GetValue());
    m_pSttsCountProperty->IncrementValue(-1 * count);
    m_pSttsSampleCountProperty->SetCount(0);
    m_pSttsSampleDeltaProperty->SetCount(0);
    
    uint64_t prev_dts = 0;
    int32_t prev_delta = -1;
    size_t sttsIndex = -1;
    std::vector<SampleTime>::iterator is;
    for (is = ++m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int32_t delta = static_cast<int32_t>(is->dts - prev_dts);
	if (delta != prev_delta) {
	    ++sttsIndex;
	    m_pSttsCountProperty->IncrementValue();
	    m_pSttsSampleCountProperty->AddValue(0);
	    m_pSttsSampleDeltaProperty->AddValue(delta);
	}
	prev_dts = is->dts;
	prev_delta = delta;
	m_pSttsSampleCountProperty->IncrementValue(1, sttsIndex);
    }
    m_pSttsSampleCountProperty->IncrementValue(1, sttsIndex);
}

int64_t MP4TrackX::UpdateCtts()
{
    int64_t maxdiff = 0;
    std::vector<SampleTime>::iterator is;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int64_t diff = is->dts - is->cts;
	if (diff > maxdiff) maxdiff = diff;
    }
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	is->cts += maxdiff;
	is->ctsOffset = is->cts - is->dts;
    }
    
    int32_t count = static_cast<int32_t>(m_pCttsCountProperty->GetValue());
    m_pCttsCountProperty->IncrementValue(-1 * count);
    m_pCttsSampleCountProperty->SetCount(0);
    m_pCttsSampleOffsetProperty->SetCount(0);

    int32_t offset = -1;
    size_t cttsIndex = -1;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	if (is->ctsOffset != offset) {
	    offset = is->ctsOffset;
	    ++cttsIndex;
	    m_pCttsCountProperty->IncrementValue();
	    m_pCttsSampleCountProperty->AddValue(0);
	    m_pCttsSampleOffsetProperty->AddValue(offset);
	}
	m_pCttsSampleCountProperty->IncrementValue(1, cttsIndex);
    }
    return maxdiff;
}

void MP4TrackX::UpdateElst(int64_t duration, int64_t mediaTime)
{
    if (!m_pElstCountProperty)
	AddEdit();
    m_pElstMediaTimeProperty->SetValue(mediaTime);
    m_pElstDurationProperty->SetValue(duration);
}
