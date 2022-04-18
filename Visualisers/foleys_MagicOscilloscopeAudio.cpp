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

void MagicOscilloscopeAudio::pushSamples (const std::shared_ptr<juce::AudioBuffer<float>> bufSP,
                                          int firstChannelToPlotIn, int numChannelsToPlotIn, int plotLengthIn)
{
  float* const* readPointers = (float*const*)(bufSP->getArrayOfReadPointers());
  int numChannelsIn = bufSP->getNumChannels();
  int firstChannelToPlot = std::min<int>(firstChannelToPlotIn, numChannelsIn-1);
  int numChansClipped = std::min<int>(numChannelsToPlotIn,numChannelsIn-firstChannelToPlot);
  // AudioBuffer (Type *const *dataToReferTo, int numChannelsToUse, int numSamples)
  juce::AudioBuffer<float> buffer(readPointers+firstChannelToPlot, numChansClipped, bufSP->getNumSamples() );
  pushSamples (buffer, plotLengthIn);
}

void MagicOscilloscopeAudio::pushSamples (const juce::AudioBuffer<float>& buffer, int plotLengthIn)
{
  const int numSamples = buffer.getNumSamples();

#if DEBUG
  float maxAmp = buffer.getMagnitude(0,numSamples);
  if (maxAmp > 0.0f) {
    // DBG("MagicOscilloscopeAudio::pushSamples: Buffer Nonzero");
  }
#endif

  plotLengthNow = std::max<int>(0,plotLengthIn);
  const int numChannelsIn = buffer.getNumChannels();
  int firstChannelToPlot = juce::jlimit(0,numChannelsIn-1,plotChannel);
  int lastChannelToPlot = std::min<int> ( numChannelsIn-1, firstChannelToPlot + numPlotChannels );
  if (overlayPlots) {
    numChannelsOut = lastChannelToPlot - firstChannelToPlot + 1;
  } else {
    averageAllChannelsToSamplesChannel0(buffer);
    numChannelsOut = 1;
  }

  // juce::AudioBuffer<float>* bufferP = &buffer;
  int firstAudibleSample[numChannelsIn];
  int lastAudibleSample[numChannelsIn];
  int startSample = 0;
  int numSamplesTrimmed = numSamples;
  if (latch) { // When latching, we don't push samples when they are inaudible (least-work method)
    // bufferP = std::unique_ptr<juce::AudioBuffer<float>>(numChannelsIn,numSamples);
    if (buffer.hasBeenCleared())
      return; // else find out if anything is audible:
    // float magnitude = buffer.getMagnitude(/* startSample */ 0, numSamples);
    // bool audible = (magnitude > 1.0E-4); // -80 dB threshold
    bool audible = false;
    for (int c=firstChannelToPlot; c<lastChannelToPlot; c++) {
      for (int s=0; s<numSamples; s++) {
        if (fabsf(buffer.getReadPointer(c)[s]) > 1.0E-4) { // -80 dB threshold
          firstAudibleSample[c] = s;
          audible = true;
          break; // This is faster than calling getMagnitude()
        }
      }
    }
    if (not audible)
      return;
    for (int c=0; c<numChannelsIn; c++) {
      for (int s=numSamples-1; s>=firstAudibleSample[c]; s--) {
        if (fabsf(buffer.getReadPointer(c)[s]) > 1.0E-4) { // -80 dB threshold
          lastAudibleSample[c] = s;
          break;
        }
      }
    }
    int firstAudibleSampleAllChannels = firstAudibleSample[0];
    int lastAudibleSampleAllChannels = lastAudibleSample[0];
    for (int c=1; c<numChannelsIn; c++) {
      firstAudibleSampleAllChannels = std::min<int> ( firstAudibleSampleAllChannels, firstAudibleSample[c] );
      lastAudibleSampleAllChannels = std::max<int> ( lastAudibleSampleAllChannels, lastAudibleSample[c] );
    }
    startSample = firstAudibleSampleAllChannels;
    numSamplesTrimmed = lastAudibleSampleAllChannels - firstAudibleSampleAllChannels + 1;
  }

  // Copy buffer samples to circular plot buffer:
  int w = writePosition.load();
  const auto available  = samples.getNumSamples() - w;
  if (available >= numSamplesTrimmed) // Copy all of the input buffer into our local ring buffer at its current write position w:
  {
    samples.copyFrom (0, w, buffer, firstChannelToPlot, startSample, numSamplesTrimmed);
    if (numChannelsOut>1) // must also copy higher channels
      {
        for (int c=firstChannelToPlot+1; c <= lastChannelToPlot; c++)
          {
            samples.copyFrom (c-firstChannelToPlot, w, buffer, c, startSample, numSamplesTrimmed);
          }
      }
  }
  else // must break up the copy into two pieces due to wraparound in the ring buffer:
  {
    samples.copyFrom (0, w, buffer, firstChannelToPlot, startSample, available);
    samples.copyFrom (0, 0, buffer, firstChannelToPlot, startSample + available, numSamplesTrimmed - available);
    if (numChannelsOut>1) // must also copy higher channels
      {
        for (int c=firstChannelToPlot+1; c < firstChannelToPlot+numChannelsOut; c++)
          {
            samples.copyFrom (c-firstChannelToPlot, w, buffer, c, startSample, available);
            samples.copyFrom (c-firstChannelToPlot, 0, buffer, c, startSample + available, numSamplesTrimmed - available);
          }
      }
  }

  checkAudioBufferForNaNs(samples);

  w += numSamplesTrimmed;
  if (available <= numSamplesTrimmed)
    w -= samples.getNumSamples();
  writePosition.store (w);

  resetLastDataFlag(); // store current time (ms) in lastData flag
}

void MagicOscilloscopeAudio::createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicAudioPlotComponent&)
{
    if (sampleRate < 20.0f || numPlotChannels < 1)
        return;

    int numPlotSamplesAvailable = samples.getNumSamples();
    int numToDisplay = getNumToDisplay(); // nominally plotLengthNow - defined in ./foleys_MagicAudioPlotSource.h
    numToDisplay = std::min<int> ( numToDisplay , numPlotSamplesAvailable);

    auto* data = samples.getReadPointer (0); // samples holds channels "plotChannel" to "plotChannel + numPlotChannels-1"

    int pos0 = writePosition.load() - numToDisplay; // nominally display the last buffer
#if 1
    int nBufs = int(pos0 / numToDisplay); // number of full buffers in samples ringbuffer
    int pos = nBufs * numToDisplay; // start at the last one and stay synchronous
#else
    int pos = getReadPosition(data, pos0); // go back to previous zero-transition if in triggered mode
#endif

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
        } else {
            // DBG("MagicOscilloscopeAudio::createPlotPaths: Signal is silent");
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
