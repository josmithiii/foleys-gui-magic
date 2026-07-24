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

#include "foleys_MagicOscilloscopeAudio.h"

namespace foleys
{


void MagicOscilloscopeAudio::checkAudioBufferForNaNs (juce::AudioBuffer<float>& buffer)
{ // Check for and clear any NaNs in :
  int nChans = buffer.getNumChannels();
  int nSamps = buffer.getNumSamples();
  int nNaNs = 0; // NaNs usually indicate parameters not getting set (no init, etc.)
  for (int c=0; c<nChans; c++) {
    float* bufP = buffer.getWritePointer(c);
    for (int n=0; n<nSamps; n++) {
      if (std::isnan(bufP[n])) {
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

void MagicOscilloscopeAudio::pushSamples (const juce::AudioBuffer<float>& bufR,
                                          int firstChannelToPlotIn, int numChannelsToPlotIn, int plotLengthIn)
{
  float* const* readPointers = (float*const*)(bufR.getArrayOfReadPointers());
  int numChannelsIn = bufR.getNumChannels();
  int firstChannelToPlot = std::min<int>(firstChannelToPlotIn, numChannelsIn-1);
  int numChansClipped = std::min<int>(numChannelsToPlotIn,numChannelsIn-firstChannelToPlot);
  // AudioBuffer (Type *const *dataToReferTo, int numChannelsToUse, int numSamples)
  juce::AudioBuffer<float> buffer(readPointers+firstChannelToPlot, numChansClipped, bufR.getNumSamples() );
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
  if ( plotLengthNow > 0 && writePosition.load() >= plotLengthNow ) // already have a plot waiting to go
      return;
  // Plotting always starts at the pushed buffer's channel 0: selecting a
  // different first channel is the pusher's job (see the pushSamples overload
  // taking firstChannelToPlot, which slices the buffer for us).
  const int lastChannelToPlot = channelsPlotted (buffer) - 1;
  if (overlayPlots) {
    numChannelsOut = lastChannelToPlot + 1;
  } else {
    averageAllChannelsToSamplesChannel0(buffer);
    numChannelsOut = 1;
  }

  int startSample = 0;
  int numSamplesTrimmed = numSamples;
  if (latch) { // When latching, we don't push samples when they are inaudible (least-work method)
    if (buffer.hasBeenCleared())
      return; // push nothing - else find out if anything is audible:
    // Audible span = the union, over the plotted channels, of the samples above
    // -80 dBFS.  Kept as a running min/max: no per-channel scratch array, so no
    // variable-length array on the audio thread (and a channel that is entirely
    // silent no longer drags the window's start back to sample 0, which is what
    // the old `firstAudibleSample[c] = 0` seed did).
    int firstAudible = numSamples;
    int lastAudible  = -1;
    for (int c = 0; c <= lastChannelToPlot; ++c) {
      const float* d = buffer.getReadPointer (c);
      for (int s = 0; s < numSamples; ++s)
        if (fabsf (d[s]) > 1.0E-4f) { // -80 dB threshold (faster than getMagnitude())
          firstAudible = std::min<int> (firstAudible, s);
          break;
        }
      for (int s = numSamples - 1; s >= 0; --s)
        if (fabsf (d[s]) > 1.0E-4f) {
          lastAudible = std::max<int> (lastAudible, s);
          break;
        }
    }
    if (lastAudible < 0)
      return; // nothing audible in any plotted channel -- keep the latched plot
    startSample = firstAudible;
    numSamplesTrimmed = lastAudible - firstAudible + 1;
    jassert (numSamplesTrimmed >= 0 && numSamplesTrimmed <= numSamples);
  }

  // Copy buffer samples to circular plot buffer:
  int w = writePosition.load(); // index of next write to samples ringbuffer

  const auto samplesBeforeWrap = samples.getNumSamples() - w; // Number of samples we can write without ringbuffer index wrap-around
  if (plotLengthNow > 0  ||  samplesBeforeWrap >= numSamplesTrimmed) // We can copy without index-wrapping
  {
    // Copy all of the input buffer into our local ring buffer at its current write position w:
    if (numChannelsOut>samples.getNumChannels()) {
        setNumChannels(numChannelsOut);
    }
    samples.copyFrom (0, w, buffer, 0, startSample, numSamplesTrimmed);
    for (int c=1; c < numChannelsOut; c++) // must also copy higher channels
      {
        samples.copyFrom (c, w, buffer, c, startSample, numSamplesTrimmed);
      }
  }
  else // must break up the copy into two pieces due to wraparound in the ring buffer:
  {
    // (the wrapped branch used to skip the setNumChannels() growth the
    //  non-wrapped one does, so a first-ever push that happened to wrap wrote
    //  past the end of `samples`)
    if (numChannelsOut>samples.getNumChannels()) {
        setNumChannels(numChannelsOut);
    }
    samples.copyFrom (0, w, buffer, 0, startSample, samplesBeforeWrap);
    samples.copyFrom (0, 0, buffer, 0, startSample + samplesBeforeWrap, numSamplesTrimmed - samplesBeforeWrap);
    for (int c=1; c < numChannelsOut; c++) // must also copy higher channels
      {
        samples.copyFrom (c, w, buffer, c, startSample, samplesBeforeWrap);
        samples.copyFrom (c, 0, buffer, c, startSample + samplesBeforeWrap, numSamplesTrimmed - samplesBeforeWrap);
      }
  }

  checkAudioBufferForNaNs(samples);

  w += numSamplesTrimmed;
  if (plotLengthNow == 0 && samplesBeforeWrap <= numSamplesTrimmed)
    w -= samples.getNumSamples();
  writePosition.store (w);
  resetLastDataFlag(); // store current time (ms) in lastData flag
}

void MagicOscilloscopeAudio::createPlotPaths (juce::Path& path, juce::Path& filledPath, juce::Rectangle<float> bounds, MagicAudioPlotComponent&)
{
    // numChannelsOut is what pushSamples() actually wrote (>= 1).  This used to
    // test numPlotChannels, which defaults to 0 -- so any AudioPlot whose layout
    // omitted `plot-num-channels` rendered permanently blank, with no warning.
    if (sampleRate < 20.0f || numChannelsOut < 1)
        return;

    if (plotLengthNow>0 && writePosition.load() < plotLengthNow)
        return; // Waiting for a complete plot to be available in plotLengthNow mode - use last plot in the meantime

    int numPlotSamplesAvailable = samples.getNumSamples();
    int numToDisplay = getNumToDisplay(); // either plotLength or plotLengthNow - defined in ./foleys_MagicAudioPlotSource.h

    auto* data = samples.getReadPointer (0); // samples holds the numChannelsOut channels pushSamples() wrote

    int pos0 = 0;
    int pos = pos0;
    if (plotLengthNow == 0) // no ringbuffer operation in this mode:
    {
      pos0 = writePosition.load() - numToDisplay; // plot most recent numToDisplay samples in ringbuffer
#if 0
      int nBufs = int(pos0 / numToDisplay); // number of full buffers in samples ringbuffer
      pos = nBufs * numToDisplay; // start at the last one and stay synchronous
#else
      pos = getReadPosition(data, pos0); // go back to previous zero-transition if in triggered mode
#endif
    }
    // Every channel must start where channel 0 does.  The higher-channel loop
    // below used to restart at the RAW pos0, which is trigger-unaligned (so the
    // overlaid channels slid against each other) and, when writePosition <
    // numToDisplay, NEGATIVE -- an out-of-bounds read.
    const int posStart = pos;

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
    // `plot-offset` separates the overlaid channels into lanes (1 == adjacent,
    // non-overlapping).  The test used to be inverted -- offsetting was applied
    // ONLY when NOT overlaying, i.e. only when numChannelsOut == 1 and nothing
    // could be offset -- so plot-offset="0.2" drew every channel on top of the
    // others.
    float plotOffsetY = (overlayPlots ? plotOffset * aPlotHeight : 0.0f);

    path.clear();
    path.startNewSubPath (plotMinX, juce::jmap (data [pos], -1.0f, 1.0f, plotMinY, plotMaxY));  // FLIPS Y

    for (int i = 1; i < numToDisplay; ++i)
    {
        ++pos;
        if (pos >= numPlotSamplesAvailable)
            pos -= numPlotSamplesAvailable;

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
        pos = posStart;
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

    if (plotLengthNow > 0)
      writePosition.store(0); // reset for next plot
}

void MagicOscilloscopeAudio::prepareToPlay (double sampleRateToUse, int samplesPerBlockExpected)
{
    MagicAudioPlotSource::prepareToPlay(sampleRateToUse, samplesPerBlockExpected);
    // Anything else needed goes here:
}


} // namespace foleys
