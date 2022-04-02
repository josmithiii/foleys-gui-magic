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
    const int numSamples = buffer.getNumSamples();
    const int numChannelsIn = buffer.getNumChannels();
    int channelToPlot = juce::jlimit(0,numChannelsIn-1,plotChannel);
    if (overlayPlots) {
        numChannelsOut = std::min<int>( numPlotChannels, numChannelsIn-plotChannel );
    } else {
        averageAllChannelsToSamplesChannel0(buffer);
        numChannelsOut = 1;
    }

    // Copy available samples:
    int w = writePosition.load();
    const auto available  = samples.getNumSamples() - w;
    numPlotChannels = std::min<int>(channelToPlot+samples.getNumChannels(),buffer.getNumChannels());
    if (available >= numSamples) // Copy all of the input buffer into our local ring buffer at its current write position w:
    {
        samples.copyFrom (0, w, buffer.getReadPointer (channelToPlot), numSamples);
        if (numChannelsOut>1) // must also copy higher channels
        {
            for (int c=channelToPlot+1; c < channelToPlot+numChannelsOut; c++)
            {
                samples.copyFrom (c-channelToPlot, w, buffer.getReadPointer (c), numSamples);
            }
        }
    }
    else // must break up the copy into two pieces due to wraparound in the ring buffer:
    {
        samples.copyFrom (0, w, buffer.getReadPointer (channelToPlot), available);
        samples.copyFrom (0, 0, buffer.getReadPointer (channelToPlot, available), numSamples - available);
        if (numChannelsOut>1) // must also copy higher channels
        {
            for (int c=channelToPlot+1; c < channelToPlot+numChannelsOut; c++)
            {
                samples.copyFrom (c-channelToPlot, w, buffer.getReadPointer (c), available);
                samples.copyFrom (c-channelToPlot, 0, buffer.getReadPointer (c, available), numSamples - available);
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
    if (sampleRate < 20.0f || numPlotChannels < 1)
        return;

    int numPlotSamplesAvailable = samples.getNumSamples();

    if (plotLength <= 0)
        plotLength = int(0.01 * sampleRate); // 10 ms default plot duration

    while (numPlotSamplesAvailable < plotLength)
        plotLength <<= 1; // cut in half until within range (better preserves desired phase)

    const auto  numToDisplay = (plotLength > 0 ?
                                std::min<int>(plotLength,samples.getNumSamples()) :
                                int (0.01 * sampleRate) - 1);

    auto* data = samples.getReadPointer (0); // samples holds channels "plotChannel" to "plotChannel + numPlotChannels-1"

    const auto pos0 = writePosition.load() - numToDisplay;
    auto pos = getReadPosition(data, pos0); // go back to previous zero-transition if in triggered mode

    // Normalize all plotted channels if requested:
    if (normalize) {
      for (int c=0; c<numChannelsOut; c++) {
        float maxAmp;
        if (pos+numToDisplay <= numPlotSamplesAvailable) {
          maxAmp = samples.getMagnitude(c,pos,numToDisplay);
        } else {
          int numToEnd = numPlotSamplesAvailable-pos;
          maxAmp = samples.getMagnitude(c,pos,numToEnd);
          maxAmp = std::max<float>(maxAmp, samples.getMagnitude(c,0,numToDisplay-numToEnd));
        }
        if (maxAmp > 1.0e-4) { // let go at -80 dB
          float ampScale = 1.0f / maxAmp;
          if (pos+numToDisplay <= numPlotSamplesAvailable) {
            samples.applyGain(c,pos,numToDisplay,ampScale); // assuming plotted sections do not overlap
          } else {
            int numToEnd = numPlotSamplesAvailable-pos;
            samples.applyGain(c,pos,numToEnd,ampScale);
            samples.applyGain(c,0,numToDisplay-numToEnd,ampScale);
          }
        }
      }
    }

    // Plot first channel:

    float plotMinX = bounds.getX();
    float plotMaxX = bounds.getRight();
    float plotMinY = bounds.getBottom();
    float plotMaxY = bounds.getY(); // (0,0) = upper-left corner => Min > Max in order to FLIP Y UPRIGHT

    float aPlotHeight = plotMaxY - plotMinY; // "algebraic" plot height - NEGATIVE since (0,0) is UPPER-left corner
    float plotOffsetY = (overlayPlots ? 0.0f : plotOffset * aPlotHeight);
    jassert(numPlotChannels>0);
    // float plotScaleY = 1.0f / float(numPlotChannels);
    // float plotHeightY = plotScaleY * aPlotHeight; // NEGATIVE - add overlapFactor?

    path.clear();
    path.startNewSubPath (plotMinX, juce::jmap (data [pos], -1.0f, 1.0f, plotMinY, plotMaxY));  // FLIPS Y

    for (int i = 1; i < numToDisplay; ++i)
    {
        ++pos;
        if (pos >= numPlotSamplesAvailable)
            pos -= numPlotSamplesAvailable;

        static bool sawNonzero = false;
        if (not sawNonzero && data[pos] != 0.0f)
        {
            sawNonzero = true;
            DBG("MagicOscilloscopeAudio::createPlotPaths: First nonzero sample to plot is " << data[pos]);
        }
        // FIXME: MAKE DOT-DASHED with 1 dot/channel, i.e., numPlotChannels dots per dash
        // Draw next point of bottom plotted channel:
        path.lineTo (juce::jmap (float (i),   0.0f, float (numToDisplay-1), plotMinX, plotMaxX),
                     juce::jmap (data [pos], -1.0f,          1.0f,          plotMinY, plotMaxY));
    } // 1st channel plot completed

    // Fill below first-channel plot only:
    filledPath = path;
    filledPath.lineTo (plotMaxX,plotMinY);
    filledPath.lineTo (plotMinX,plotMinY);
    filledPath.closeSubPath(); // includes path.lineTo (plotMinX,data[pos])

    // path.closeSubPath(); // draw from end of plot back to beginning (ok if both at minY or maxY)

    // Plot higher channels, if any:
    for (int c=1; c<numChannelsOut; c++)
    {
        data = samples.getReadPointer (c);
        pos = pos0;
        path.startNewSubPath (bounds.getX(),
                              juce::jmap (data [pos], -1.0f, 1.0f, plotMinY+c*plotOffsetY, plotMaxY+c*plotOffsetY));
        for (int i = 1; i < numToDisplay; ++i)
        {
            ++pos;
            if (pos >= numPlotSamplesAvailable)
                pos -= numPlotSamplesAvailable;

            // FIXME: MAKE DOT-DASHED with 1 dot/channel, i.e., numPlotChannels dots per dash
            path.lineTo (juce::jmap (float (i),   0.0f,  float (numToDisplay-1), plotMinX, plotMaxX),
                         juce::jmap (data [pos], -1.0f,  1.0f,   plotMinY+c*plotOffsetY, plotMaxY+c*plotOffsetY));
        }
        // FIXME: Consider fill here
        // path.closeSubPath(); // draw from end of plot back to beginning (ok if both at minY or maxY)
    }
}

void MagicOscilloscopeAudio::prepareToPlay (double sampleRateToUse, int samplesPerBlockExpected)
{
    MagicAudioPlotSource::prepareToPlay(sampleRateToUse, samplesPerBlockExpected);
    // Anything else needed goes here:
}


} // namespace foleys
