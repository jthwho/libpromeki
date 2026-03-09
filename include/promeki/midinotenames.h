/**
 * @file      midinotenames.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/array.h>
#include <promeki/string.h>
#include <promeki/midinote.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Provides a customizable name overlay for MIDI notes.
 *
 * MidiNoteNames holds an optional name for each of the 128 MIDI note
 * values.  When a name is set for a note, name() returns it; otherwise
 * it falls back to MidiNote::name() (e.g. "C4").
 *
 * Use the static factory methods to load well-known name sets such as
 * General MIDI percussion.
 */
class MidiNoteNames {
        public:
                static constexpr int NumNotes = 128;

                /** @brief Constructs an empty name set (all notes fall back to MidiNote::name()). */
                MidiNoteNames() = default;

                /**
                 * @brief Returns the name for a MIDI note.
                 *
                 * If a custom name has been set for this note, it is returned.
                 * Otherwise, the default MidiNote::name() is returned.
                 */
                String name(MidiNote note) const {
                        if(!note.isValid()) return String();
                        const String &n = _names[note.rawValue()];
                        if(!n.isEmpty()) return n;
                        return note.name();
                }

                /** @brief Sets a custom name for a MIDI note. */
                void setName(MidiNote note, const String &name) {
                        if(note.isValid()) _names[note.rawValue()] = name;
                }

                /** @brief Clears the custom name for a MIDI note (reverts to default). */
                void clearName(MidiNote note) {
                        if(note.isValid()) _names[note.rawValue()] = String();
                }

                /** @brief Returns true if a custom name is set for the given note. */
                bool hasName(MidiNote note) const {
                        if(!note.isValid()) return false;
                        return !_names[note.rawValue()].isEmpty();
                }

                /** @brief Creates a name set with General MIDI percussion names. */
                static MidiNoteNames gmPercussion();

        private:
                Array<String, NumNotes> _names;
};

PROMEKI_NAMESPACE_END
