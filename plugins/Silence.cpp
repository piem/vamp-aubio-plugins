/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Vamp feature extraction plugins using Paul Brossier's Aubio library.

    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.

*/

#include <math.h>
#include "Silence.h"

using std::string;
using std::vector;
using std::cerr;
using std::endl;

Silence::Silence(float inputSampleRate) :
    Plugin(inputSampleRate),
    m_ibuf(0),
    m_pbuf(0),
    m_tmpptrs(0),
    m_threshold(-70),
    m_prevSilent(false),
    m_first(true)
{
}

Silence::~Silence()
{
    if (m_ibuf) del_fvec(m_ibuf);
    if (m_pbuf) del_fvec(m_pbuf);
    if (m_tmpptrs) delete[] m_tmpptrs;
}

string
Silence::getIdentifier() const
{
    return "aubiosilence";
}

string
Silence::getName() const
{
    return "Aubio Silence Detector";
}

string
Silence::getDescription() const
{
    return "Detect levels below a certain threshold";
}

string
Silence::getMaker() const
{
    return "Paul Brossier (plugin by Chris Cannam)";
}

int
Silence::getPluginVersion() const
{
    return 1;
}

string
Silence::getCopyright() const
{
    return "GPL";
}

bool
Silence::initialise(size_t channels, size_t stepSize, size_t blockSize)
{
    m_channelCount = channels;
    m_stepSize = stepSize;
    m_blockSize = blockSize;

    m_ibuf = new_fvec(stepSize, channels);
    m_pbuf = new_fvec(stepSize, channels);
    m_tmpptrs = new smpl_t *[channels];

    return true;
}

void
Silence::reset()
{
    m_first = true;
}

size_t
Silence::getPreferredStepSize() const
{
    return 1024;
}

size_t
Silence::getPreferredBlockSize() const
{
    return 1024;
}

Silence::ParameterList
Silence::getParameterDescriptors() const
{
    ParameterList list;
    ParameterDescriptor desc;

    desc = ParameterDescriptor();
    desc.identifier = "silencethreshold";
    desc.name = "Silence Threshold";
    desc.minValue = -120;
    desc.maxValue = 0;
    desc.defaultValue = -70;
    desc.unit = "dB";
    desc.isQuantized = false;
    list.push_back(desc);

    return list;
}

float
Silence::getParameter(std::string param) const
{
    if (param == "silencethreshold") {
        return m_threshold;
    } else {
        return 0.0;
    }
}

void
Silence::setParameter(std::string param, float value)
{
    if (param == "silencethreshold") {
        m_threshold = value;
    }
}

Silence::OutputList
Silence::getOutputDescriptors() const
{
    OutputList list;

    OutputDescriptor d;
    d.identifier = "silencestart";
    d.name = "Starts of Silent Regions";
    d.description = "Return a single instant at the point where each silent region begins";
    d.hasFixedBinCount = true;
    d.binCount = 0;
    d.sampleType = OutputDescriptor::VariableSampleRate;
    list.push_back(d);

    d.identifier = "silenceend";
    d.name = "Ends of Silent Regions";
    d.description = "Return a single instant at the point where each silent region ends";
    d.hasFixedBinCount = true;
    d.binCount = 0;
    d.sampleType = OutputDescriptor::VariableSampleRate;
    list.push_back(d);

    d.identifier = "silencelevel";
    d.name = "Silence Test";
    d.description = "Return a function that switches from 1 to 0 when silence falls, and back again when it ends";
    d.hasFixedBinCount = true;
    d.binCount = 1;
    d.hasKnownExtents = true;
    d.minValue = 0;
    d.maxValue = 1;
    d.isQuantized = true;
    d.quantizeStep = 1;
    d.sampleType = OutputDescriptor::VariableSampleRate;
    list.push_back(d);

    return list;
}

Silence::FeatureSet
Silence::process(const float *const *inputBuffers,
                 Vamp::RealTime timestamp)
{
    for (size_t i = 0; i < m_stepSize; ++i) {
        for (size_t j = 0; j < m_channelCount; ++j) {
            fvec_write_sample(m_ibuf, inputBuffers[j][i], j, i);
        }
    }

    bool silent = aubio_silence_detection(m_ibuf, m_threshold);
    FeatureSet returnFeatures;

    if (m_first || m_prevSilent != silent) {

        Vamp::RealTime featureStamp = timestamp;

        if ((silent && !m_first) || !silent) {
        
            // refine our result

            long off = 0;
            size_t incr = 16;
            if (incr > m_stepSize/8) incr = m_stepSize/8;

            fvec_t vec;
            vec.length = incr * 4;
            vec.channels = m_channelCount;
            vec.data = m_tmpptrs;
            
            if (silent) {
                std::cerr << "silence at " << timestamp << std::endl;
            }
            
            for (size_t i = 0; i < m_stepSize - incr * 4; i += incr) {
                for (size_t j = 0; j < m_channelCount; ++j) {
                    m_tmpptrs[j] = m_ibuf->data[j] + i;
                }
                bool subsilent = aubio_silence_detection(&vec, m_threshold);
                if (silent == subsilent) {
                    std::cerr << "silent == subsilent at " << i << " after" << std::endl;
                    off = i;
                    break;
                }
            }

            if (silent && (off == 0)) {
                for (size_t i = 0; i < m_stepSize - incr; i += incr) {
                    for (size_t j = 0; j < m_channelCount; ++j) {
                        m_tmpptrs[j] = m_pbuf->data[j] + m_stepSize - i - incr;
                    }
                    bool subsilent = aubio_silence_detection(&vec, m_threshold);
                    if (!subsilent) {
                        std::cerr << "non-silence at " << i << " samples before" << std::endl;
                        off = -(long)i;
                        break;
                    } else {
                        std::cerr << "silence at " << i << " samples before" << std::endl;
                    }
                }
            } else {
            }                

            featureStamp = timestamp + Vamp::RealTime::frame2RealTime
                (off, lrintf(m_inputSampleRate));
        }

        Feature feature;
        feature.hasTimestamp = true;
        feature.timestamp = featureStamp;
        feature.values.push_back(silent ? 0 : 1);
        returnFeatures[2].push_back(feature);
        feature.values.clear();

        if (silent) {
            returnFeatures[0].push_back(feature);
        } else {
            returnFeatures[1].push_back(feature);
        }            

        m_prevSilent = silent;
        m_first = false;
    }

    // swap ibuf and pbuf data pointers, so that this block's data is
    // available in pbuf when processing the next block, without
    // having to allocate new storage for it
    smpl_t **tmpdata = m_ibuf->data;
    m_ibuf->data = m_pbuf->data;
    m_pbuf->data = tmpdata;

    return returnFeatures;
}

Silence::FeatureSet
Silence::getRemainingFeatures()
{
    return FeatureSet();
}

