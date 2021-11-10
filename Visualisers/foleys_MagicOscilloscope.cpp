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


namespace foleys
{


MagicOscilloscope::MagicOscilloscope (int channelToDisplay)
  : MagicPlotAudioSource(channelToDisplay)
{
}

void MagicOscilloscope::pushSamples (const juce::AudioBuffer<float>& buffer)
{
    const int  numChannelsIn = buffer.getNumChannels();
    int numChannelsOut = numChannelsIn; // until determined otherwise

    if (channel >= 0) {
      numChannelsOut = 1;
    }

    bool averageChannels = (numChannelsOut>1) && not overlayPlots;

    if (averageChannels)
    {
      averageAllChannelsToSamplesChannel0(buffer);
      numChannelsOut = 1;
    }

    jassert(channel >= 0);

    // Copy available samples
    int numSamples = buffer.getNumSamples();
    int w = writePosition.load();
    const auto available  = samples.getNumSamples() - w;
    if (available >= numSamples)
    {
        if (overlayPlots)
        {
            samples.copyFrom (0, w, buffer.getReadPointer (channel), numSamples);
        } else {
            samples.copyFrom (0, w, buffer, numSamples);
        }
        if (numChannelsOut>1) {
          ...
        }
    }
    else
    {
        samples.copyFrom (0, w, buffer.getReadPointer (channel),            available);
        samples.copyFrom (0, 0, buffer.getReadPointer (channel, available), numSamples - available);
        if (numChannelsOut>1) {
          ...
        }
    }

    if (available > numSamples)
        writePosition.store (w + numSamples);
    else
        writePosition.store (numSamples - available);

    resetLastDataFlag();
}

void MagicOscilloscope::createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicPlotComponent&)
{
    if (sampleRate < 20.0f)
        return;

    const auto  numToDisplay = int (0.01 * sampleRate) - 1;
    const auto* data = samples.getReadPointer (0);

    auto pos = writePosition.load() - numToDisplay;
    if (pos < 0)
        pos += samples.getNumSamples();

    if (triggered) // find first zero-crossing in circular plot-buffer samplesX, giving up after 50 ms <-> 20 Hz fundamental:
    {
        auto positive = data [pos] > 0.0f;
        auto bail = int (sampleRate / 20.0f);

        while (positive == false && --bail > 0)
        {
            if (--pos < 0)
                pos += samples.getNumSamples();

            positive = data [pos] > 0.0f;
        }

        while (positive == true && --bail > 0)
        {
            if (--pos < 0)
                pos += samples.getNumSamples();

            positive = data [pos] > 0.0f;
        }
    }

    path.clear();
    path.startNewSubPath (bounds.getX(),
                          juce::jmap (data [pos], -1.0f, 1.0f, bounds.getBottom(), bounds.getY()));

    for (int i = 1; i < numToDisplay; ++i)
    {
        ++pos;
        if (pos >= samples.getNumSamples())
            pos -= samples.getNumSamples();

        path.lineTo (juce::jmap (float (i),   0.0f, float (numToDisplay), bounds.getX(), bounds.getRight()),
                     juce::jmap (data [pos], -1.0f, 1.0f,                 bounds.getBottom(), bounds.getY()));
    }

    filledPath = path;
    filledPath.lineTo (bounds.getBottomRight());
    filledPath.lineTo (bounds.getBottomLeft());
    filledPath.closeSubPath();
}

void MagicOscilloscope::prepareToPlay (double sampleRateToUse, int)
{
    sampleRate = sampleRateToUse;

    samples.setSize (1, static_cast<int> (sampleRate));
    samples.clear();

    writePosition.store (0);
}


} // namespace foleys
