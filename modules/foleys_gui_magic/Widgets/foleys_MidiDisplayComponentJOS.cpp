/*
 ==============================================================================
    Copyright (c) 2023 Julius Smith - made from foleys_MidiLearnComponent.h:
    Copyright (c) 2019-2022 Foleys Finest Audio - Daniel Walz
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

#include "foleys_MidiDisplayComponentJOS.h"

namespace foleys
{

void MidiDisplayComponent::setMagicProcessorState (MagicProcessorState* state)
{
    processorState = state;
    startTimerHz (4);
}

void MidiDisplayComponent::paint (juce::Graphics& g)
{
    if (processorState)
    {
      auto channel = processorState->getLastMidiChannel();
      auto text = juce::String("Channel: " + (channel > 0 ? juce::String (channel) : "unknown"));

      auto note = processorState->getLastMidiNote();
      text += ("\nNote: " + (note > 0 ? juce::String (note) : "unknown"));

      auto velocity = processorState->getLastMidiVelocity();
      text += ("\nVelocity: " + (velocity > 0 ? juce::String (velocity) : "unknown"));

      auto cc = processorState->getLastController();
      text += ("\nCC: " + (cc > 0 ? juce::String (cc) : "unknown"));

      auto pitchBend = processorState->getLastPitchBend();
      // Display as signed value: -8192 to +8191 (0 = center)
      text += ("\nPitchBend: " + juce::String (pitchBend - 8192));

      g.setColour (juce::Colours::silver);
      g.drawFittedText (text, getLocalBounds(), juce::Justification::centred, 5);
    }
}

void MidiDisplayComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (processorState && event.mouseWasDraggedSinceMouseDown())
    {
        auto cc = processorState->getLastController();
        if (cc < 1)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            container->startDragging (IDs::dragCC + juce::String (cc), this);
        }
    }
}

void MidiDisplayComponent::timerCallback()
{
    repaint();
}


}
