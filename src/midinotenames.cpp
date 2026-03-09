/**
 * @file      midinotenames.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/midinotenames.h>

PROMEKI_NAMESPACE_BEGIN

MidiNoteNames MidiNoteNames::gmPercussion() {
        MidiNoteNames names;
        names.setName(MidiNote::GMP_HighQ,            "High Q");
        names.setName(MidiNote::GMP_Slap,             "Slap");
        names.setName(MidiNote::GMP_ScratchPush,      "Scratch Push");
        names.setName(MidiNote::GMP_ScratchPull,      "Scratch Pull");
        names.setName(MidiNote::GMP_Sticks,           "Sticks");
        names.setName(MidiNote::GMP_SquareClick,      "Square Click");
        names.setName(MidiNote::GMP_MetronomeClick,   "Metronome Click");
        names.setName(MidiNote::GMP_MetronomeBell,    "Metronome Bell");
        names.setName(MidiNote::GMP_AcousticBassDrum, "Acoustic Bass Drum");
        names.setName(MidiNote::GMP_BassDrum1,        "Bass Drum 1");
        names.setName(MidiNote::GMP_SideStick,        "Side Stick");
        names.setName(MidiNote::GMP_AcousticSnare,    "Acoustic Snare");
        names.setName(MidiNote::GMP_HandClap,         "Hand Clap");
        names.setName(MidiNote::GMP_ElectricSnare,    "Electric Snare");
        names.setName(MidiNote::GMP_LowFloorTom,      "Low Floor Tom");
        names.setName(MidiNote::GMP_ClosedHiHat,      "Closed Hi-Hat");
        names.setName(MidiNote::GMP_HighFloorTom,     "High Floor Tom");
        names.setName(MidiNote::GMP_PedalHiHat,       "Pedal Hi-Hat");
        names.setName(MidiNote::GMP_LowTom,           "Low Tom");
        names.setName(MidiNote::GMP_OpenHiHat,        "Open Hi-Hat");
        names.setName(MidiNote::GMP_LowMidTom,        "Low-Mid Tom");
        names.setName(MidiNote::GMP_HiMidTom,         "Hi-Mid Tom");
        names.setName(MidiNote::GMP_CrashCymbal1,     "Crash Cymbal 1");
        names.setName(MidiNote::GMP_HighTom,          "High Tom");
        names.setName(MidiNote::GMP_RideCymbal1,      "Ride Cymbal 1");
        names.setName(MidiNote::GMP_ChineseCymbal,    "Chinese Cymbal");
        names.setName(MidiNote::GMP_RideBell,         "Ride Bell");
        names.setName(MidiNote::GMP_Tambourine,       "Tambourine");
        names.setName(MidiNote::GMP_SplashCymbal,     "Splash Cymbal");
        names.setName(MidiNote::GMP_Cowbell,          "Cowbell");
        names.setName(MidiNote::GMP_CrashCymbal2,     "Crash Cymbal 2");
        names.setName(MidiNote::GMP_Vibraslap,        "Vibraslap");
        names.setName(MidiNote::GMP_RideCymbal2,      "Ride Cymbal 2");
        names.setName(MidiNote::GMP_HiBongo,          "Hi Bongo");
        names.setName(MidiNote::GMP_LowBongo,         "Low Bongo");
        names.setName(MidiNote::GMP_MuteHiConga,      "Mute Hi Conga");
        names.setName(MidiNote::GMP_OpenHiConga,      "Open Hi Conga");
        names.setName(MidiNote::GMP_LowConga,         "Low Conga");
        names.setName(MidiNote::GMP_HighTimbale,      "High Timbale");
        names.setName(MidiNote::GMP_LowTimbale,       "Low Timbale");
        names.setName(MidiNote::GMP_HighAgogo,        "High Agogo");
        names.setName(MidiNote::GMP_LowAgogo,         "Low Agogo");
        names.setName(MidiNote::GMP_Cabasa,           "Cabasa");
        names.setName(MidiNote::GMP_Maracas,          "Maracas");
        names.setName(MidiNote::GMP_ShortWhistle,     "Short Whistle");
        names.setName(MidiNote::GMP_LongWhistle,      "Long Whistle");
        names.setName(MidiNote::GMP_ShortGuiro,       "Short Guiro");
        names.setName(MidiNote::GMP_LongGuiro,        "Long Guiro");
        names.setName(MidiNote::GMP_Claves,           "Claves");
        names.setName(MidiNote::GMP_HiWoodBlock,      "Hi Wood Block");
        names.setName(MidiNote::GMP_LowWoodBlock,     "Low Wood Block");
        names.setName(MidiNote::GMP_MuteCuica,        "Mute Cuica");
        names.setName(MidiNote::GMP_OpenCuica,        "Open Cuica");
        names.setName(MidiNote::GMP_MuteTriangle,     "Mute Triangle");
        names.setName(MidiNote::GMP_OpenTriangle,     "Open Triangle");
        names.setName(MidiNote::GMP_Shaker,           "Shaker");
        names.setName(MidiNote::GMP_JingleBell,       "Jingle Bell");
        names.setName(MidiNote::GMP_Belltree,         "Belltree");
        names.setName(MidiNote::GMP_Castanets,        "Castanets");
        names.setName(MidiNote::GMP_MuteSurdo,        "Mute Surdo");
        names.setName(MidiNote::GMP_OpenSurdo,        "Open Surdo");
        return names;
}

PROMEKI_NAMESPACE_END
