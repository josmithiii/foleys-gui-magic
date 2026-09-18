/*
 ==============================================================================
    Copyright (c) 2019-2023 Foleys Finest Audio - Daniel Walz
    All rights reserved.

    **BSD 3-Clause License**

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

 ==============================================================================

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

#pragma once

namespace foleys
{

/**
 The MidiParameterMapper allows to connect CC values to RangedAudioParameters
 */
class MidiParameterMapper  : private juce::ValueTree::Listener
{
public:
    MidiParameterMapper (MagicProcessorState& state);
    ~MidiParameterMapper() override;

    /*!
     * Get Midi CC messages and set parameters accordingly
     * @param buffer the last midi events
     */
    void processMidiBuffer (juce::MidiBuffer& buffer);

    /*!
     * The SAMPLE-RANGE form: apply only the events whose sample position lies in
     * [startSample, startSample + numSamples), so a host that splits its block
     * at a control event can let that event land on its own sample instead of at
     * the top of the buffer.
     *
     * Added for jos-juce-plugins' PERFORMABLE_PARAMETERS_PLAN.md M3: a performed
     * parameter is dezippered per sample, which only helps if the target it is
     * gliding towards was set where the player actually moved the control.  The
     * whole-buffer form above is exactly this one over the whole buffer, so
     * nothing that does not split changes at all.
     *
     * @param buffer the last midi events
     * @param startSample the first sample position to honour
     * @param numSamples how many samples the range covers
     */
    void processMidiBuffer (juce::MidiBuffer& buffer, int startSample, int numSamples);

    /*!
     * Is this CC number currently MIDI-learned to at least one parameter?
     * A caller that wants to split its audio block at control events needs to
     * know which events are worth splitting for, and asking the mapper is the
     * only way that cannot drift from what the mapper will actually do.
     *
     * Real-time safe and lock-free: it takes the same tryEnter() the audio path
     * takes, and answers false rather than blocking if the map is being edited.
     */
    bool isMappedController (int ccNumber);

    /**
     * Map a MIDI CC to a parameter.
     *
     * Behavior:
     * - If the exact mapping (same CC + same parameter) already exists, it is removed
     *   (toggle off / "MIDI Unlearn" by re-dragging the same CC).
     * - If a different CC was previously mapped to this parameter, the old mapping is
     *   removed and replaced with the new one (one CC per parameter).
     * - One CC can still control multiple parameters (drag same CC to different controls).
     *
     * @param cc the MIDI CC number to map (1-127)
     * @param parameterID the parameterID to map to
     */
    void mapMidiController (int cc, const juce::String& parameterID);

    /*!
     * Remove a specific mapping
     * @param cc the MIDI CC number to map
     * @param parameterID the parameterID to unmap
     */
    void unmapMidiController (int cc, const juce::String& parameterID);

    /*!
     * Remove all mappings from a specific CC controller
     * @param cc the MIDI CC number to unmap
     */
    void unmapAllMidiController (int cc);

    /*!
     * @return the last touched MIDI controller so it can be mapped
     */
    int  getLastController() const;

    /*!
     * @return the last MIDI Channel on which MIDI was received by MIDI Mapper
     */
    int  getLastMidiChannel() const;

    /*!
     * @return the last MIDI Note received by MIDI Mapper
     */
    int  getLastMidiNote() const;

    /*!
     * @return the last MIDI Velocity received by MIDI Mapper
     */
    int  getLastMidiVelocity() const;

    /*!
     * @return the last MIDI Pitch Bend value received (0-16383, 8192 = center)
     */
    int  getLastPitchBend() const;

    /*!
     * Grant access to the ValueTree to save or restore the mappings manually
     * @return the ValueTree containing the mappings
     */
    juce::ValueTree getMappingSettings();

private:
    /**
     * Rebuild the internal midiMapper from the settings ValueTree.
     * Called when mappings change. Uses non-blocking tryEnter() on mappingLock
     * to avoid blocking the GUI thread when the audio thread is processing MIDI.
     * If the lock cannot be acquired, the update is skipped (next change will retry).
     */
    void recreateMidiMapper();

    void valueTreeChildAdded (juce::ValueTree& parentTree,
                              juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree&, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;


    using MidiMapping=std::map<int, std::vector<juce::RangedAudioParameter*>>;

    SharedApplicationSettings   settings;
    juce::CriticalSection       mappingLock;

    MagicProcessorState&        state;
    std::atomic<int>            lastController { -1 };
    std::atomic<int>            lastMidiChannel { -1 };
    std::atomic<int>            lastMidiNote { -1 };
    std::atomic<int>            lastMidiVelocity { -1 };
    std::atomic<int>            lastPitchBend { 8192 };  // center = no bend
    MidiMapping                 midiMapper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiParameterMapper)
};

} // namespace foleys
