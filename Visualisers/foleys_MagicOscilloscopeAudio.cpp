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


MagicOscilloscopeAudio::MagicOscilloscopeAudio (int channelToDisplay)
  : MagicAudioPlotSource(channelToDisplay)
{
}

void MagicOscilloscopeAudio::checkAudioBufferForNaNs (juce::AudioBuffer<float>& buffer)
{ // Check for and clear any NaNs in :
  int nChans = buffer.getNumChannels();
  int nSamps = buffer.getNumSamples();
  int nNaNs = 0; // NaNs usually indicate parameters not getting set (no init, etc.)
  for (int c=0; c<nChans; c++) {
    float* bufP = buffer.getWritePointer(c);
    for (int n=0; n<nSamps; n++) {
      if (isnan(bufP[n])) {
        bufP[n] = 0.0f;
        nNaNs++;
      }
    }
  }
  if (nNaNs>0) {
    std::cerr << "*** MagicOscilloscopeAudio.cpp: Have " << nNaNs << " NaNs!\n";
  }
}

void MagicOscilloscopeAudio::pushSamples (const juce::AudioBuffer<float>& buffer)
{
    const int  numChannelsIn = buffer.getNumChannels();
    int numChannelsOut = numChannelsIn; // until determined otherwise
  
    // checkAudioBufferForNaNs(buffer);

    if (channel >= 0) {
      numChannelsOut = 1;
    } else {
      channel = 0; // default
    }

    bool averageChannels = (numChannelsOut>1) && not overlayPlots;

    if (averageChannels)
    {
      averageAllChannelsToSamplesChannel0(buffer);
      numChannelsOut = 1;
    }

    jassert(channel >= 0);

    // Copy available samples
    int w = writePosition.load();
    const auto available  = samples.getNumSamples() - w;
    int numSamples = buffer.getNumSamples();
    if (available >= numSamples) // Copy all of the input buffer into our local ring buffer at its current write position w:
    {
        samples.copyFrom (0, w, buffer.getReadPointer (channel), numSamples);
        if (numChannelsOut>1 && overlayPlots) // must also copy higher channels
        {
            for (int c=channel+1; c < std::min<int>(samples.getNumChannels(),buffer.getNumChannels()); c++)
            {
                  samples.copyFrom (c-channel, w, buffer.getReadPointer (c), numSamples);
            }
        }
    }
    else // must break up the copy into two pieces due to wraparound in the ring buffer:
    {
        samples.copyFrom (0, w, buffer.getReadPointer (channel), available);
        samples.copyFrom (0, 0, buffer.getReadPointer (channel, available), numSamples - available);
        if (numChannelsOut>1 && overlayPlots) // must also copy higher channels
        {
            for (int c=channel+1; c < std::min<int>(samples.getNumChannels(),buffer.getNumChannels()); c++)
            {
                samples.copyFrom (c-channel, w, buffer.getReadPointer (c), available);
                samples.copyFrom (c-channel, 0, buffer.getReadPointer (c, available), numSamples - available);
            }
        }
    }
  
    checkAudioBufferForNaNs(samples);

    if (available > numSamples)
        writePosition.store (w + numSamples);
    else
        writePosition.store (numSamples - available);

    resetLastDataFlag();
}

void MagicOscilloscopeAudio::createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicAudioPlotComponent&)
{
    if (sampleRate < 20.0f)
        return;

    const auto  numToDisplay = (plotLength > 0 ?
                                std::min<int>(plotLength,samples.getNumSamples()) :
                                int (0.01 * sampleRate) - 1);

    auto* data = samples.getReadPointer (0);

    const auto pos0 = writePosition.load() - numToDisplay;
    auto pos = getReadPosition(data, pos0); // advance to next zero-crossing if in triggered mode

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

    int numChannelsOut = samples.getNumChannels();
    if (numChannelsOut>1 && overlayPlots) // must also draw higher channels
    {
        path.closeSubPath(); // interestingly we leave the last subpath open!
        float currOffsetY = 0.0f;
        float plotOffsetY = plotOffset * bounds.getHeight();
        for (int c=1; c<numChannelsOut; c++)
        {
            data = samples.getReadPointer (c);
            pos = pos0;
            currOffsetY += plotOffsetY;
            path.startNewSubPath (bounds.getX(),
                                  juce::jmap (data [pos] + currOffsetY, -1.0f, 1.0f, bounds.getBottom(), bounds.getY()));

            for (int i = 1; i < numToDisplay; ++i)
            {
                ++pos;
                if (pos >= samples.getNumSamples())
                    pos -= samples.getNumSamples();

                path.lineTo (juce::jmap (float (i),   0.0f, float (numToDisplay), bounds.getX(), bounds.getRight()),
                             juce::jmap (data [pos] + currOffsetY, -1.0f, 1.0f,   bounds.getBottom(), bounds.getY()));
            }
        }
    }
}

void MagicOscilloscopeAudio::prepareToPlay (double sampleRateToUse, int samplesPerBlockExpected)
{
    MagicAudioPlotSource::prepareToPlay(sampleRateToUse, samplesPerBlockExpected);
    // Anything else needed goes here:
}


} // namespace foleys
