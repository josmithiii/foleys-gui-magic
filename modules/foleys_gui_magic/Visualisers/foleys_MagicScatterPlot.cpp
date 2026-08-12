/*
 ==============================================================================
    Copyright (c) 2019-2021 Foleys Finest Audio - Daniel Walz
    All rights reserved.

    License for non-commercial projects:

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    License for commercial products:

    To sell commercial products containing this module, you are required to buy a
    License from https://foleysfinest.com/developer/pluginguimagic/

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
    OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
    OF THE POSSIBILITY OF SUCH DAMAGE.
 ==============================================================================
 */

#include "foleys_MagicScatterPlot.h"

namespace foleys
{

void MagicScatterPlot::pushSamples (const juce::AudioBuffer<float>& bufferIn, int currentPlotLengthIn)
{
  const int numChannels = bufferIn.getNumChannels();
  if (numChannels < 1)
    return; // nothing to scatter (the channel indices below would go negative)
  // X = channel 0, Y = channel 1 -- or channel 0 against itself (the 45-degree
  // diagonal) when only one channel was pushed.
  const int chanX = 0;
  const int chanY = std::min<int>(1,numChannels-1);
  pushSamples(/* bufferX */ bufferIn, chanX, /* bufferY */ bufferIn, chanY, currentPlotLengthIn);
}

void MagicScatterPlot::pushSamples (const juce::AudioBuffer<float>& bufferX, int channelX,
                                    const juce::AudioBuffer<float>& bufferY, int channelY,
                                    int plotLengthOverride)
{
    auto w = writePosition.load();

    plotLengthNow = std::max<int>(0,plotLengthOverride);

    const auto numSamples = bufferX.getNumSamples();
    jassert(numSamples == bufferY.getNumSamples());
    const auto available  = samplesX.getNumSamples() - w;

    const auto numChannels = bufferX.getNumChannels();
    jassert(numChannels == bufferY.getNumChannels());

    // plot (channelX,channelY):

    if (available >= numSamples)
    {
        samplesX.copyFrom (0, w, bufferX.getReadPointer (channelX), numSamples);
        samplesY.copyFrom (0, w, bufferY.getReadPointer (channelY), numSamples);
    }
    else
    {
        samplesX.copyFrom (0, w, bufferX.getReadPointer (channelX), available);
        samplesY.copyFrom (0, w, bufferY.getReadPointer (channelY), available);
        samplesX.copyFrom (0, 0, bufferX.getReadPointer (channelX, available), numSamples - available);
        samplesY.copyFrom (0, 0, bufferY.getReadPointer (channelY, available), numSamples - available);
    }

    if (available > numSamples)
        writePosition.store (w + numSamples);
    else
        writePosition.store (numSamples - available);

    detectClipping (bufferX.getReadPointer (channelX),
                    bufferY.getReadPointer (channelY), numSamples);

    resetLastDataFlag();
}

// ====================================================================================================

void MagicScatterPlot::detectClipping (const float* dataX, const float* dataY, int numSamples)
{
    // Audio thread.  Sides: 0 = right (x > 1), 1 = left (x < -1),
    // 2 = top (y > 1), 3 = bottom (y < -1).  `along` is the free coordinate,
    // clamped onto the square, so the marker sits where the trace escaped.
    // NaN compares false everywhere and is skipped.
    auto mark = [this] (int side, float along)
    {
        along = juce::jlimit (-1.0f, 1.0f, along);
        const int cell = juce::jlimit (0, kCellsPerSide - 1,
                                       int ((along + 1.0f) * 0.5f * float (kCellsPerSide)));
        const juce::uint32 bit = juce::uint32 (1) << cell;
        if ((clipCells[side].fetch_or (bit) & bit) != 0)
            return;                                   // this cell already has its marker
        const int n = numClipMarkers.load (std::memory_order_relaxed);
        if (n >= kMaxClipMarkers)
            return;                                   // full (cannot happen before every cell is lit)
        switch (side)
        {
            case 0: clipMarkers[size_t (n)] = {  1.0f, along }; break;
            case 1: clipMarkers[size_t (n)] = { -1.0f, along }; break;
            case 2: clipMarkers[size_t (n)] = { along,  1.0f }; break;
            default: clipMarkers[size_t (n)] = { along, -1.0f }; break;
        }
        numClipMarkers.store (n + 1, std::memory_order_release);
    };

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = dataX[i];
        const float y = dataY[i];
        if (x >  1.0f) mark (0, y);
        if (x < -1.0f) mark (1, y);
        if (y >  1.0f) mark (2, x);
        if (y < -1.0f) mark (3, x);
    }
}

void MagicScatterPlot::clearClipMarkers()
{
    numClipMarkers.store (0, std::memory_order_release);
    for (auto& cells : clipCells)
        cells.store (0);
}

void MagicScatterPlot::drawDecorations (juce::Graphics& g, juce::Rectangle<float> bounds,
                                        MagicAudioPlotComponent& component)
{
    // The unit square: |sample| = 1, which createPlotPaths' jmap puts exactly
    // on the component bounds.  Reference geometry, so it takes the plot
    // colour, dimmed.
    g.setColour (component.findColour (MagicAudioPlotComponent::plotColourId).withAlpha (0.35f));
    g.drawRect (bounds, 1.0f);

    const int n = numClipMarkers.load (std::memory_order_acquire);
    if (n == 0)
        return;

    // Clipping is red everywhere in audio; these must not be mistakable for
    // the trace.  Inset the centres by the marker radius so a marker on the
    // square survives the component's clip region.
    const float r = 3.0f;
    const auto inner = bounds.reduced (r);
    g.setColour (juce::Colours::red);
    for (int i = 0; i < n; ++i)
    {
        const auto m = clipMarkers[size_t (i)];
        const float px = juce::jmap (m.x, -1.0f, 1.0f, inner.getX(), inner.getRight());
        const float py = juce::jmap (m.y, -1.0f, 1.0f, inner.getBottom(), inner.getY());
        g.fillEllipse (px - r, py - r, 2.0f * r, 2.0f * r);
    }
}

// ====================================================================================================

void MagicScatterPlot::createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicAudioPlotComponent&)
{
    if (sampleRate < 20.0f)
        return;

    const auto numToDisplay = getNumToDisplay(); // nominally plotLengthNow - defined in ./foleys_MagicAudioPlotSource.h
    const auto* dataX = samplesX.getReadPointer (0);
    const auto* dataY = samplesY.getReadPointer (0);

    auto position = writePosition.load() - numToDisplay;
    if (position < 0)
        position += samplesX.getNumSamples();

    if (triggeredPos || triggeredNeg) // find first zero-crossing in circular plot-buffer samplesX, giving up after 50 ms <-> 20 Hz fundamental:
    {
        auto positive = dataX [position] > 0.0f;
        auto bail = int (sampleRate / 20.0f);

        while (positive == false && --bail > 0)
        {
            if (--position < 0)
                position += samplesX.getNumSamples();

            positive = dataX [position] > 0.0f;
        }

        while (positive == true && --bail > 0)
        {
            if (--position < 0)
                position += samplesX.getNumSamples();

            positive = dataX [position] > 0.0f;
        }
    }

    // FIXME: Sum channels here if X and Y are multichannel and overlay is false

    path.clear();
    path.startNewSubPath (juce::jmap (dataX [position], -1.0f, 1.0f, bounds.getX(), bounds.getRight()),
                          juce::jmap (dataY [position], -1.0f, 1.0f, bounds.getBottom(), bounds.getY()));

    for (int i = 1; i < numToDisplay; ++i)
    {
        ++position;
        if (position >= samplesX.getNumSamples())
            position -= samplesX.getNumSamples();

        path.lineTo (juce::jmap (dataX [position], -1.0f, 1.0f, bounds.getX(), bounds.getRight()),
                     juce::jmap (dataY [position], -1.0f, 1.0f, bounds.getBottom(), bounds.getY()));
    }

    // FIXME: Make more paths here if X and Y are multichannel and overlay is true

    filledPath = path;
    filledPath.lineTo (bounds.getBottomRight());
    filledPath.lineTo (bounds.getBottomLeft());
    filledPath.closeSubPath();
}

void MagicScatterPlot::prepareToPlay (double sampleRateToUse, int)
{
    sampleRate = sampleRateToUse;

    samplesX.setSize (1, static_cast<int> (sampleRate));
    samplesX.clear();

    samplesY.setSize (1, static_cast<int> (sampleRate));
    samplesY.clear();

    writePosition.store (0);
}


} // namespace foleys
