//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================

#include <map>

#include "ac/character.h"
#include "ac/common.h"
#include "ac/dialogtopic.h"
#include "ac/draw.h"
#include "ac/dynamicsprite.h"
#include "ac/game.h"
#include "ac/gamesetupstruct.h"
#include "ac/gamestate.h"
#include "ac/gui.h"
#include "ac/mouse.h"
#include "ac/movelist.h"
#include "ac/roomstatus.h"
#include "ac/roomstruct.h"
#include "ac/screenoverlay.h"
#include "ac/spritecache.h"
#include "ac/view.h"
#include "ac/system.h"
#include "ac/dynobj/cc_serializer.h"
#include "debug/out.h"
#include "game/savegame_components.h"
#include "gfx/bitmap.h"
#include "gui/animatingguibutton.h"
#include "gui/guibutton.h"
#include "gui/guiinv.h"
#include "gui/guilabel.h"
#include "gui/guilistbox.h"
#include "gui/guimain.h"
#include "gui/guislider.h"
#include "gui/guitextbox.h"
#include "media/audio/audio.h"
#include "media/audio/soundclip.h"
#include "plugin/agsplugin.h"
#include "plugin/plugin_engine.h"
#include "script/cc_error.h"
#include "script/script.h"
#include "util/filestream.h" // TODO: needed only because plugins expect file handle

using namespace Common;

extern GameSetupStruct game;
extern color palette[256];
extern DialogTopic *dialog;
extern AnimatingGUIButton animbuts[MAX_ANIMATING_BUTTONS];
extern int numAnimButs;
extern ViewStruct *views;
extern ScreenOverlay screenover[MAX_SCREEN_OVERLAYS];
extern int numscreenover;
extern Bitmap *dynamicallyCreatedSurfaces[MAX_DYNAMIC_SURFACES];
extern roomstruct thisroom;
extern RoomStatus troom;
extern Bitmap *raw_saved_screen;
extern MoveList *mls;


namespace AGS
{
namespace Engine
{

namespace SavegameComponents
{

const String ComponentListTag = "Components";

void WriteFormatTag(PStream out, const String &tag, bool open = true)
{
    String full_tag = String::FromFormat(open ? "<%s>" : "</%s>", tag.GetCStr());
    out->Write(full_tag.GetCStr(), full_tag.GetLength());
}

bool ReadFormatTag(PStream in, String &tag, bool open = true)
{
    if (in->ReadByte() != '<')
        return false;
    if (!open && in->ReadByte() != '/')
        return false;
    tag.Empty();
    while (!in->EOS())
    {
        char c = in->ReadByte();
        if (c == '>')
            return true;
        tag.AppendChar(c);
    }
    return false; // reached EOS before closing symbol
}

bool AssertFormatTag(PStream in, const String &tag, bool open = true)
{
    String read_tag;
    if (!ReadFormatTag(in, read_tag, open))
        return false;
    return read_tag.Compare(tag) == 0;
}

inline bool AssertCompatLimit(int count, int max_count, const char *content_name)
{
    if (count > max_count)
    {
        Debug::Printf(kDbgMsg_Error, "Restore game error: incompatible number of %s (count: %d, max: %d)",
            content_name, count, max_count);
        return false;
    }
    return true;
}

inline bool AssertCompatRange(int value, int min_value, int max_value, const char *content_name)
{
    if (value < min_value || value > max_value)
    {
        Debug::Printf(kDbgMsg_Error, "Restore game error: incompatible %s (id: %d, range: %d - %d)",
            content_name, value, min_value, max_value);
        return false;
    }
    return true;
}

inline bool AssertGameContent(int new_val, int original_val, const char *content_name)
{
    if (new_val != original_val)
    {
        Debug::Printf(kDbgMsg_Error, "Restore game error: mismatching number of %s (game: %d, save: %d)",
            content_name, original_val, new_val);
        return false;
    }
    return true;
}

inline bool AssertGameObjectContent(int new_val, int original_val, const char *content_name,
                                    const char *obj_type, int obj_id)
{
    if (new_val != original_val)
    {
        Debug::Printf(kDbgMsg_Error, "Restore game error: mismatching number of %s, %s #%d (game: %d, save: %d)",
            content_name, obj_type, obj_id, original_val, new_val);
        return false;
    }
    return true;
}

inline bool AssertGameObjectContent2(int new_val, int original_val, const char *content_name,
                                    const char *obj1_type, int obj1_id, const char *obj2_type, int obj2_id)
{
    if (new_val != original_val)
    {
        Debug::Printf(kDbgMsg_Error, "Restore game error: mismatching number of %s, %s #%d, %s #%d (game: %d, save: %d)",
            content_name, obj1_type, obj1_id, obj2_type, obj2_id, original_val, new_val);
        return false;
    }
    return true;
}


SavegameError WriteGameState(PStream out)
{
    // Game base
    game.WriteForSavegame(out);
    // Game palette
    // TODO: probably no need to save this for hi/true-res game
    out->WriteArray(palette, sizeof(color), 256);

    if (loaded_game_file_version <= kGameVersion_272)
    {
        // Global variables
        out->WriteInt32(numGlobalVars);
        for (int i = 0; i < numGlobalVars; ++i)
            globalvars[i].Write(out.get());
    }

    // Game state
    play.WriteForSavegame(out.get());
    // Other dynamic values
    out->WriteInt32(frames_per_second);
    out->WriteInt32(loopcounter);
    out->WriteInt32(ifacepopped);
    out->WriteInt32(game_paused);
    // Mouse cursor
    out->WriteInt32(cur_mode);
    out->WriteInt32(cur_cursor);
    out->WriteInt32(mouse_on_iface);
    // Viewport
    out->WriteInt32(offsetx);
    out->WriteInt32(offsety);
    return kSvgErr_NoError;
}

SavegameError ReadGameState(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    // Game base
    game.ReadFromSavegame(in);
    // Game palette
    in->ReadArray(palette, sizeof(color), 256);

    if (loaded_game_file_version <= kGameVersion_272)
    {
        // Legacy interaction global variables
        if (!AssertGameContent(in->ReadInt32(), numGlobalVars, "Global Variables"))
            return kSvgErr_GameContentAssertion;
        for (int i = 0; i < numGlobalVars; ++i)
            globalvars[i].Read(in.get());
    }

    // Game state
    play.ReadFromSavegame(in.get(), false);

    // Other dynamic values
    r_data.FPS = in->ReadInt32();
    loopcounter = in->ReadInt32();
    ifacepopped = in->ReadInt32();
    game_paused = in->ReadInt32();
    // Mouse cursor state
    r_data.CursorMode = in->ReadInt32();
    r_data.CursorID = in->ReadInt32();
    mouse_on_iface = in->ReadInt32();
    // Viewport state
    offsetx = in->ReadInt32();
    offsety = in->ReadInt32();
    return kSvgErr_NoError;
}

SavegameError WriteAudio(PStream out)
{
    // Game content assertion
    out->WriteInt32(game.audioClipTypeCount);
    out->WriteInt32(game.audioClipCount);
    // Audio types
    for (int i = 0; i < game.audioClipTypeCount; ++i)
    {
        game.audioClipTypes[i].WriteToSavegame(out.get());
        out->WriteInt32(play.default_audio_type_volumes[i]);
    }

    // Audio clips and crossfade
    for (int i = 0; i <= MAX_SOUND_CHANNELS; i++)
    {
        if ((channels[i] != NULL) && (channels[i]->done == 0) && (channels[i]->sourceClip != NULL))
        {
            out->WriteInt32(((ScriptAudioClip*)channels[i]->sourceClip)->id);
            out->WriteInt32(channels[i]->get_pos());
            out->WriteInt32(channels[i]->priority);
            out->WriteInt32(channels[i]->repeat ? 1 : 0);
            out->WriteInt32(channels[i]->vol);
            out->WriteInt32(channels[i]->panning);
            out->WriteInt32(channels[i]->volAsPercentage);
            out->WriteInt32(channels[i]->panningAsPercentage);
            out->WriteInt32(channels[i]->speed);
        }
        else
        {
            out->WriteInt32(-1);
        }
    }
    out->WriteInt32(crossFading);
    out->WriteInt32(crossFadeVolumePerStep);
    out->WriteInt32(crossFadeStep);
    out->WriteInt32(crossFadeVolumeAtStart);
    // CHECKME: why this needs to be saved?
    out->WriteInt32(current_music_type);

    // Ambient sound
    for (int i = 0; i < MAX_SOUND_CHANNELS; ++i)
        ambient[i].WriteToFile(out.get());
    return kSvgErr_NoError;
}

SavegameError ReadAudio(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    // Game content assertion
    if (!AssertGameContent(in->ReadInt32(), game.audioClipTypeCount, "Audio Clip Types"))
        return kSvgErr_GameContentAssertion;
    if (!AssertGameContent(in->ReadInt32(), game.audioClipCount, "Audio Clips"))
        return kSvgErr_GameContentAssertion;

    // Audio types
    for (int i = 0; i < game.audioClipTypeCount; ++i)
    {
        game.audioClipTypes[i].ReadFromSavegame(in.get());
        play.default_audio_type_volumes[i] = in->ReadInt32();
    }

    // Audio clips and crossfade
    for (int i = 0; i <= MAX_SOUND_CHANNELS; ++i)
    {
        RestoredData::ChannelInfo &chan_info = r_data.AudioChans[i];
        chan_info.Pos = 0;
        chan_info.ClipID = in->ReadInt32();
        if (chan_info.ClipID >= 0)
        {
            chan_info.Pos = in->ReadInt32();
            if (chan_info.Pos < 0)
                chan_info.Pos = 0;
            chan_info.Priority = in->ReadInt32();
            chan_info.Repeat = in->ReadInt32();
            chan_info.Vol = in->ReadInt32();
            chan_info.Pan = in->ReadInt32();
            chan_info.VolAsPercent = in->ReadInt32();
            chan_info.PanAsPercent = in->ReadInt32();
            chan_info.Speed = 1000;
            chan_info.Speed = in->ReadInt32();
        }
    }
    crossFading = in->ReadInt32();
    crossFadeVolumePerStep = in->ReadInt32();
    crossFadeStep = in->ReadInt32();
    crossFadeVolumeAtStart = in->ReadInt32();
    // preserve legacy music type setting
    current_music_type = in->ReadInt32();
    
    // Ambient sound
    for (int i = 0; i < MAX_SOUND_CHANNELS; ++i)
        ambient[i].ReadFromFile(in.get());
    for (int i = 1; i < MAX_SOUND_CHANNELS; ++i)
    {
        if (ambient[i].channel == 0)
        {
            r_data.DoAmbient[i] = 0;
        }
        else
        {
            r_data.DoAmbient[i] = ambient[i].num;
            ambient[i].channel = 0;
        }
    }
    return kSvgErr_NoError;
}

SavegameError WriteCharacters(PStream out)
{
    out->WriteInt32(game.numcharacters);
    for (int i = 0; i < game.numcharacters; ++i)
    {
        game.chars[i].WriteToFile(out.get());
        charextra[i].WriteToFile(out.get());
        Properties::WriteValues(play.charProps[i], out.get());
        if (loaded_game_file_version <= kGameVersion_272)
            game.intrChar[i]->WriteTimesRunToSavedgame(out.get());
        // character movement path cache
        mls[CHMLSOFFS + i].WriteToFile(out.get());
    }
    return kSvgErr_NoError;
}

SavegameError ReadCharacters(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    if (!AssertGameContent(in->ReadInt32(), game.numcharacters, "Characters"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < game.numcharacters; ++i)
    {
        game.chars[i].ReadFromFile(in.get());
        charextra[i].ReadFromFile(in.get());
        Properties::ReadValues(play.charProps[i], in.get());
        if (loaded_game_file_version <= kGameVersion_272)
            game.intrChar[i]->ReadTimesRunFromSavedgame(in.get());
        // character movement path cache
        mls[CHMLSOFFS + i].ReadFromFile(in.get());
    }
    return kSvgErr_NoError;
}

SavegameError WriteDialogs(PStream out)
{
    out->WriteInt32(game.numdialog);
    for (int i = 0; i < game.numdialog; ++i)
    {
        dialog[i].WriteToSavegame(out.get());
    }
    return kSvgErr_NoError;
}

SavegameError ReadDialogs(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    if (!AssertGameContent(in->ReadInt32(), game.numdialog, "Dialogs"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < game.numdialog; ++i)
    {
        dialog[i].ReadFromSavegame(in.get());
    }
    return kSvgErr_NoError;
}

SavegameError WriteGUI(PStream out)
{
    // GUI state
    WriteFormatTag(out, "GUIs");
    out->WriteInt32(game.numgui);
    for (int i = 0; i < game.numgui; ++i)
        guis[i].WriteToSavegame(out.get());

    WriteFormatTag(out, "GUIButtons");
    out->WriteInt32(numguibuts);
    for (int i = 0; i < numguibuts; ++i)
        guibuts[i].WriteToSavegame(out.get());

    WriteFormatTag(out, "GUILabels");
    out->WriteInt32(numguilabels);
    for (int i = 0; i < numguilabels; ++i)
        guilabels[i].WriteToSavegame(out.get());

    WriteFormatTag(out, "GUIInvWindows");
    out->WriteInt32(numguiinv);
    for (int i = 0; i < numguiinv; ++i)
        guiinv[i].WriteToSavegame(out.get());

    WriteFormatTag(out, "GUISliders");
    out->WriteInt32(numguislider);
    for (int i = 0; i < numguislider; ++i)
        guislider[i].WriteToSavegame(out.get());

    WriteFormatTag(out, "GUITextBoxes");
    out->WriteInt32(numguitext);
    for (int i = 0; i < numguitext; ++i)
        guitext[i].WriteToSavegame(out.get());

    WriteFormatTag(out, "GUIListBoxes");
    out->WriteInt32(numguilist);
    for (int i = 0; i < numguilist; ++i)
        guilist[i].WriteToSavegame(out.get());

    // Animated buttons
    WriteFormatTag(out, "AnimatedButtons");
    out->WriteInt32(numAnimButs);
    for (int i = 0; i < numAnimButs; ++i)
        animbuts[i].WriteToFile(out.get());
    return kSvgErr_NoError;
}

SavegameError ReadGUI(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    // GUI state
    if (!AssertFormatTag(in, "GUIs"))
        return kSvgErr_InconsistentFormat;
    if (!AssertGameContent(in->ReadInt32(), game.numgui, "GUIs"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < game.numgui; ++i)
        guis[i].ReadFromSavegame(in.get());

    if (!AssertFormatTag(in, "GUIButtons"))
        return kSvgErr_InconsistentFormat;
    if (!AssertGameContent(in->ReadInt32(), numguibuts, "GUI Buttons"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < numguibuts; ++i)
        guibuts[i].ReadFromSavegame(in.get());

    if (!AssertFormatTag(in, "GUILabels"))
        return kSvgErr_InconsistentFormat;
    if (!AssertGameContent(in->ReadInt32(), numguilabels, "GUI Labels"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < numguilabels; ++i)
        guilabels[i].ReadFromSavegame(in.get());

    if (!AssertFormatTag(in, "GUIInvWindows"))
        return kSvgErr_InconsistentFormat;
    if (!AssertGameContent(in->ReadInt32(), numguiinv, "GUI InvWindows"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < numguiinv; ++i)
        guiinv[i].ReadFromSavegame(in.get());

    if (!AssertFormatTag(in, "GUISliders"))
        return kSvgErr_InconsistentFormat;
    if (!AssertGameContent(in->ReadInt32(), numguislider, "GUI Sliders"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < numguislider; ++i)
        guislider[i].ReadFromSavegame(in.get());

    if (!AssertFormatTag(in, "GUITextBoxes"))
        return kSvgErr_InconsistentFormat;
    if (!AssertGameContent(in->ReadInt32(), numguitext, "GUI TextBoxes"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < numguitext; ++i)
        guitext[i].ReadFromSavegame(in.get());

    if (!AssertFormatTag(in, "GUIListBoxes"))
        return kSvgErr_InconsistentFormat;
    if (!AssertGameContent(in->ReadInt32(), numguilist, "GUI ListBoxes"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < numguilist; ++i)
        guilist[i].ReadFromSavegame(in.get());

    // Animated buttons
    if (!AssertFormatTag(in, "AnimatedButtons"))
        return kSvgErr_InconsistentFormat;
    int anim_count = in->ReadInt32();
    if (!AssertCompatLimit(anim_count, MAX_ANIMATING_BUTTONS, "animated buttons"))
        return kSvgErr_IncompatibleEngine;
    numAnimButs = anim_count;
    for (int i = 0; i < numAnimButs; ++i)
        animbuts[i].ReadFromFile(in.get());
    return kSvgErr_NoError;
}

SavegameError WriteInventory(PStream out)
{
    out->WriteInt32(game.numinvitems);
    for (int i = 0; i < game.numinvitems; ++i)
    {
        game.invinfo[i].WriteToSavegame(out.get());
        Properties::WriteValues(play.invProps[i], out.get());
        if (loaded_game_file_version <= kGameVersion_272)
            game.intrInv[i]->WriteTimesRunToSavedgame(out.get());
    }
    return kSvgErr_NoError;
}

SavegameError ReadInventory(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    if (!AssertGameContent(in->ReadInt32(), game.numinvitems, "Inventory Items"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < game.numinvitems; ++i)
    {
        game.invinfo[i].ReadFromSavegame(in.get());
        Properties::ReadValues(play.invProps[i], in.get());
        if (loaded_game_file_version <= kGameVersion_272)
            game.intrInv[i]->ReadTimesRunFromSavedgame(in.get());
    }
    return kSvgErr_NoError;
}

SavegameError WriteMouseCursors(PStream out)
{
    out->WriteInt32(game.numcursors);
    for (int i = 0; i < game.numcursors; ++i)
    {
        game.mcurs[i].WriteToSavegame(out.get());
    }
    return kSvgErr_NoError;
}

SavegameError ReadMouseCursors(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    if (!AssertGameContent(in->ReadInt32(), game.numcursors, "Mouse Cursors"))
        return kSvgErr_GameContentAssertion;
    for (int i = 0; i < game.numcursors; ++i)
    {
        game.mcurs[i].ReadFromSavegame(in.get());
    }
    return kSvgErr_NoError;
}

SavegameError WriteViews(PStream out)
{
    out->WriteInt32(game.numviews);
    for (int view = 0; view < game.numviews; ++view)
    {
        out->WriteInt32(views[view].numLoops);
        for (int loop = 0; loop < views[view].numLoops; ++loop)
        {
            out->WriteInt32(views[view].loops[loop].numFrames);
            for (int frame = 0; frame < views[view].loops[loop].numFrames; ++frame)
            {
                out->WriteInt32(views[view].loops[loop].frames[frame].sound);
                out->WriteInt32(views[view].loops[loop].frames[frame].pic);
            }
        }
    }
    return kSvgErr_NoError;
}

SavegameError ReadViews(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    if (!AssertGameContent(in->ReadInt32(), game.numviews, "Views"))
        return kSvgErr_GameContentAssertion;
    for (int view = 0; view < game.numviews; ++view)
    {
        if (!AssertGameObjectContent(in->ReadInt32(), views[view].numLoops,
            "Loops", "View", view))
            return kSvgErr_GameContentAssertion;
        for (int loop = 0; loop < views[view].numLoops; ++loop)
        {
            if (!AssertGameObjectContent2(in->ReadInt32(), views[view].loops[loop].numFrames,
                "Frame", "View", view, "Loop", loop))
                return kSvgErr_GameContentAssertion;
            for (int frame = 0; frame < views[view].loops[loop].numFrames; ++frame)
            {
                views[view].loops[loop].frames[frame].sound = in->ReadInt32();
                views[view].loops[loop].frames[frame].pic = in->ReadInt32();
            }
        }
    }
    return kSvgErr_NoError;
}

SavegameError WriteDynamicSprites(PStream out)
{
    const size_t ref_pos = out->GetPosition();
    out->WriteInt32(0); // number of dynamic sprites
    out->WriteInt32(0); // top index
    int count = 0;
    int top_index = 1;
    for (int i = 1; i < spriteset.elements; ++i)
    {
        if (game.spriteflags[i] & SPF_DYNAMICALLOC)
        {
            count++;
            top_index = i;
            out->WriteInt32(i);
            out->WriteInt32(game.spriteflags[i]);
            serialize_bitmap(spriteset[i], out.get());
        }
    }
    const size_t end_pos = out->GetPosition();
    out->Seek(ref_pos, kSeekBegin);
    out->WriteInt32(count);
    out->WriteInt32(top_index);
    out->Seek(end_pos, kSeekBegin);
    return kSvgErr_NoError;
}

SavegameError ReadDynamicSprites(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    const int spr_count = in->ReadInt32();
    // ensure the sprite set is at least large enough
    // to accomodate top dynamic sprite index
    const int top_index = in->ReadInt32();
    if (!AssertCompatRange(top_index, 1, MAX_SPRITES - 1, "sprite top index"))
        return kSvgErr_IncompatibleEngine;
    spriteset.enlargeTo(top_index);
    for (int i = 0; i < spr_count; ++i)
    {
        int id = in->ReadInt32();
        if (!AssertCompatRange(id, 1, MAX_SPRITES - 1, "sprite index"))
            return kSvgErr_IncompatibleEngine;
        int flags = in->ReadInt32();
        add_dynamic_sprite(id, read_serialized_bitmap(in.get()));
        game.spriteflags[id] = flags;
    }
    return kSvgErr_NoError;
}

SavegameError WriteOverlays(PStream out)
{
    out->WriteInt32(numscreenover);
    for (int i = 0; i < numscreenover; ++i)
    {
        screenover[i].WriteToFile(out.get());
        serialize_bitmap(screenover[i].pic, out.get());
    }
    return kSvgErr_NoError;
}

SavegameError ReadOverlays(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    int over_count = in->ReadInt32();
    if (!AssertCompatLimit(over_count, MAX_SCREEN_OVERLAYS, "overlays"))
        return kSvgErr_IncompatibleEngine;
    numscreenover = over_count;
    for (int i = 0; i < numscreenover; ++i)
    {
        screenover[i].ReadFromFile(in.get());
        if (screenover[i].hasSerializedBitmap)
            screenover[i].pic = read_serialized_bitmap(in.get());
    }
    return kSvgErr_NoError;
}

SavegameError WriteDynamicSurfaces(PStream out)
{
    out->WriteInt32(MAX_DYNAMIC_SURFACES);
    for (int i = 0; i < MAX_DYNAMIC_SURFACES; ++i)
    {
        if (dynamicallyCreatedSurfaces[i] == NULL)
        {
            out->WriteInt8(0);
        }
        else
        {
            out->WriteInt8(1);
            serialize_bitmap(dynamicallyCreatedSurfaces[i], out.get());
        }
    }
    return kSvgErr_NoError;
}

SavegameError ReadDynamicSurfaces(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    if (!AssertCompatLimit(in->ReadInt32(), MAX_DYNAMIC_SURFACES, "Dynamic Surfaces"))
        return kSvgErr_GameContentAssertion;
    // Load the surfaces into a temporary array since ccUnserialiseObjects will destroy them otherwise
    r_data.DynamicSurfaces.resize(MAX_DYNAMIC_SURFACES);
    for (int i = 0; i < MAX_DYNAMIC_SURFACES; ++i)
    {
        if (in->ReadInt8() == 0)
            r_data.DynamicSurfaces[i] = NULL;
        else
            r_data.DynamicSurfaces[i] = read_serialized_bitmap(in.get());
    }
    return kSvgErr_NoError;
}

SavegameError WriteScriptModules(PStream out)
{
    // write the data segment of the global script
    int data_len = gameinst->globaldatasize;
    out->WriteInt32(data_len);
    if (data_len > 0)
        out->Write(gameinst->globaldata, data_len);
    // write the script modules data segments
    out->WriteInt32(numScriptModules);
    for (int i = 0; i < numScriptModules; ++i)
    {
        data_len = moduleInst[i]->globaldatasize;
        out->WriteInt32(data_len);
        if (data_len > 0)
            out->Write(moduleInst[i]->globaldata, data_len);
    }
    return kSvgErr_NoError;
}

SavegameError ReadScriptModules(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    // read the global script data segment
    int data_len = in->ReadInt32();
    if (!AssertGameContent(data_len, pp.GlScDataSize, "global script data"))
        return kSvgErr_GameContentAssertion;
    r_data.GlobalScript.Len = data_len;
    r_data.GlobalScript.Data.reset(new char[data_len]);
    in->Read(r_data.GlobalScript.Data.get(), data_len);

    if (!AssertGameContent(in->ReadInt32(), numScriptModules, "Script Modules"))
        return kSvgErr_GameContentAssertion;
    r_data.ScriptModules.resize(numScriptModules);
    for (int i = 0; i < numScriptModules; ++i)
    {
        data_len = in->ReadInt32();
        if (!AssertGameObjectContent(data_len, pp.ScMdDataSize[i], "script module data", "module", i))
            return kSvgErr_GameContentAssertion;
        r_data.ScriptModules[i].Len = data_len;
        r_data.ScriptModules[i].Data.reset(new char[data_len]);
        in->Read(r_data.ScriptModules[i].Data.get(), data_len);
    }
    return kSvgErr_NoError;
}

SavegameError WriteRoomStates(PStream out)
{
    // write the room state for all the rooms the player has been in
    out->WriteInt32(MAX_ROOMS);
    for (int i = 0; i < MAX_ROOMS; ++i)
    {
        if (isRoomStatusValid(i))
        {
            RoomStatus *roomstat = getRoomStatus(i);
            if (roomstat->beenhere)
            {
                out->WriteInt32(i);
                WriteFormatTag(out, "RoomState", true);
                roomstat->WriteToSavegame(out.get());
                WriteFormatTag(out, "RoomState", false);
            }
            else
                out->WriteInt32(-1);
        }
        else
            out->WriteInt32(-1);
    }
    return kSvgErr_NoError;
}

SavegameError ReadRoomStates(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    int roomstat_count = in->ReadInt32();
    for (; roomstat_count > 0; --roomstat_count)
    {
        int id = in->ReadInt32();
        // If id == -1, then the player has not been there yet (or room state was reset)
        if (id != -1)
        {
            if (!AssertCompatRange(id, 0, MAX_ROOMS - 1, "room index"))
                return kSvgErr_IncompatibleEngine;
            if (!AssertFormatTag(in, "RoomState", true))
                return kSvgErr_InconsistentFormat;
            RoomStatus *roomstat = getRoomStatus(id);
            roomstat->ReadFromSavegame(in.get());
            if (!AssertFormatTag(in, "RoomState", false))
                return kSvgErr_InconsistentFormat;
        }
    }
    return kSvgErr_NoError;
}

SavegameError WriteThisRoom(PStream out)
{
    out->WriteInt32(displayed_room);
    if (displayed_room < 0)
        return kSvgErr_NoError;

    // modified room backgrounds
    for (int i = 0; i < MAX_BSCENE; ++i)
    {
        out->WriteBool(play.raw_modified[i] != 0);
        if (play.raw_modified[i])
            serialize_bitmap(thisroom.ebscene[i], out.get());
    }
    out->WriteBool(raw_saved_screen != NULL);
    if (raw_saved_screen)
        serialize_bitmap(raw_saved_screen, out.get());

    // room region state
    for (int i = 0; i < MAX_REGIONS; ++i)
    {
        out->WriteInt32(thisroom.regionLightLevel[i]);
        out->WriteInt32(thisroom.regionTintLevel[i]);
    }
    for (int i = 0; i < MAX_WALK_AREAS + 1; ++i)
    {
        out->WriteInt32(thisroom.walk_area_zoom[i]);
        out->WriteInt32(thisroom.walk_area_zoom2[i]);
    }

    // room object movement paths cache
    out->WriteInt32(thisroom.numsprs + 1);
    for (int i = 0; i < thisroom.numsprs + 1; ++i)
    {
        mls[i].WriteToFile(out.get());
    }

    // room music volume
    out->WriteInt32(thisroom.options[ST_VOLUME]);

    // persistent room's indicator
    const bool persist = displayed_room < MAX_ROOMS;
    out->WriteBool(persist);
    // write the current troom state, in case they save in temporary room
    if (!persist)
        troom.WriteToSavegame(out.get());
    return kSvgErr_NoError;
}

SavegameError ReadThisRoom(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    displayed_room = in->ReadInt32();
    if (displayed_room < 0)
        return kSvgErr_NoError;

    // modified room backgrounds
    for (int i = 0; i < MAX_BSCENE; ++i)
    {
        play.raw_modified[i] = in->ReadBool();
        if (play.raw_modified[i])
            r_data.RoomBkgScene[i] = read_serialized_bitmap(in.get());
        else
            r_data.RoomBkgScene[i] = NULL;
    }
    if (in->ReadBool())
        raw_saved_screen = read_serialized_bitmap(in.get());

    // room region state
    for (int i = 0; i < MAX_REGIONS; ++i)
    {
        r_data.RoomLightLevels[i] = in->ReadInt32();
        r_data.RoomTintLevels[i] = in->ReadInt32();
    }
    for (int i = 0; i < MAX_WALK_AREAS + 1; ++i)
    {
        r_data.RoomZoomLevels1[i] = in->ReadInt32();
        r_data.RoomZoomLevels2[i] = in->ReadInt32();
    }

    // room object movement paths cache
    int objmls_count = in->ReadInt32();
    if (!AssertCompatLimit(objmls_count, CHMLSOFFS, "room object move lists"))
        return kSvgErr_IncompatibleEngine;
    for (int i = 0; i < objmls_count; ++i)
    {
        mls[i].ReadFromFile(in.get());
    }

    // save the new room music vol for later use
    r_data.RoomVolume = in->ReadInt32();

    // read the current troom state, in case they saved in temporary room
    if (!in->ReadBool())
        troom.ReadFromSavegame(in.get());

    return kSvgErr_NoError;
}

SavegameError WriteManagedPool(PStream out)
{
    ccSerializeAllObjects(out.get());
    return kSvgErr_NoError;
}

SavegameError ReadManagedPool(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    if (ccUnserializeAllObjects(in.get(), &ccUnserializer))
    {
        Debug::Printf(kDbgMsg_Error, "Restore game error: managed pool deserialization failed: %s", ccErrorString);
        return kSvgErr_GameObjectInitFailed;
    }
    return kSvgErr_NoError;
}

SavegameError WritePluginData(PStream out)
{
    // [IKM] Plugins expect FILE pointer! // TODO something with this later...
    pl_run_plugin_hooks(AGSE_SAVEGAME, (long)((Common::FileStream*)out.get())->GetHandle());
    return kSvgErr_NoError;
}

SavegameError ReadPluginData(PStream in, int32_t cmp_ver, const PreservedParams &pp, RestoredData &r_data)
{
    // [IKM] Plugins expect FILE pointer! // TODO something with this later
    pl_run_plugin_hooks(AGSE_RESTOREGAME, (long)((Common::FileStream*)in.get())->GetHandle());
    return kSvgErr_NoError;
}


// Description of a supported game state serialization component
struct ComponentHandler
{
    String             Name;
    int32_t            Version;
    SavegameError      (*Serialize)  (PStream);
    SavegameError      (*Unserialize)(PStream, int32_t cmp_ver, const PreservedParams&, RestoredData&);
};

// Array of supported components
ComponentHandler ComponentHandlers[] =
{
    {
        "Game State",
        0,
        WriteGameState,
        ReadGameState
    },
    {
        "Audio",
        0,
        WriteAudio,
        ReadAudio
    },
    {
        "Characters",
        0,
        WriteCharacters,
        ReadCharacters
    },
    {
        "Dialogs",
        0,
        WriteDialogs,
        ReadDialogs
    },
    {
        "GUI",
        0,
        WriteGUI,
        ReadGUI
    },
    {
        "Inventory Items",
        0,
        WriteInventory,
        ReadInventory
    },
    {
        "Mouse Cursors",
        0,
        WriteMouseCursors,
        ReadMouseCursors
    },
    {
        "Views",
        0,
        WriteViews,
        ReadViews
    },
    {
        "Dynamic Sprites",
        0,
        WriteDynamicSprites,
        ReadDynamicSprites
    },
    {
        "Overlays",
        0,
        WriteOverlays,
        ReadOverlays
    },
    {
        "Dynamic Surfaces",
        0,
        WriteDynamicSurfaces,
        ReadDynamicSurfaces
    },
    {
        "Script Modules",
        0,
        WriteScriptModules,
        ReadScriptModules
    },
    {
        "Room States",
        0,
        WriteRoomStates,
        ReadRoomStates
    },
    {
        "Loaded Room State",
        0,
        WriteThisRoom,
        ReadThisRoom
    },
    {
        "Managed Pool",
        0,
        WriteManagedPool,
        ReadManagedPool
    },
    {
        "Plugin Data",
        0,
        WritePluginData,
        ReadPluginData
    },
    { NULL, 0, NULL, NULL } // end of array
};


typedef std::map<String, ComponentHandler> HandlersMap;
void GenerateHandlersMap(HandlersMap &map)
{
    map.clear();
    for (int i = 0; !ComponentHandlers[i].Name.IsEmpty(); ++i)
        map[ComponentHandlers[i].Name] = ComponentHandlers[i];
}

// A helper struct to pass to (de)serialization handlers
struct SvgCmpReadHelper
{
    SavegameVersion       Version;  // general savegame version
    const PreservedParams &PP;      // previous game state kept for reference
    RestoredData          &RData;   // temporary storage for loaded data, that
                                    // will be applied after loading is done
    // The map of serialization handlers, one per supported component type ID
    HandlersMap            Handlers;

    SvgCmpReadHelper(SavegameVersion svg_version, const PreservedParams &pp, RestoredData &r_data)
        : Version(svg_version)
        , PP(pp)
        , RData(r_data)
    {
    }
};

// The basic information about deserialized component, used for debugging purposes
struct ComponentInfo
{
    String  Name;
    int32_t Version;
    size_t  Offset;     // offset at which an opening tag is located
    size_t  DataOffset; // offset at which component data begins
    size_t  DataSize;   // expected size of component data

    ComponentInfo() : Version(-1), Offset(0), DataOffset(0), DataSize(0) {}
};

SavegameError ReadComponent(PStream in, SvgCmpReadHelper &hlp, ComponentInfo &info)
{
    size_t pos = in->GetPosition();
    info = ComponentInfo(); // reset in case of early error
    info.Offset = in->GetPosition();
    if (!ReadFormatTag(in, info.Name, true))
        return kSvgErr_ComponentOpeningTagFormat;
    info.Version = in->ReadInt32();
    info.DataSize = in->ReadInt32();
    info.DataOffset = in->GetPosition();

    const ComponentHandler *handler = NULL;
    std::map<String, ComponentHandler>::const_iterator it_hdr = hlp.Handlers.find(info.Name);
    if (it_hdr != hlp.Handlers.end())
        handler = &it_hdr->second;

    if (!handler || !handler->Unserialize)
        return kSvgErr_UnsupportedComponent;
    if (info.Version > handler->Version)
        return kSvgErr_UnsupportedComponentVersion;
    SavegameError err = handler->Unserialize(in, info.Version, hlp.PP, hlp.RData);
    if (err != kSvgErr_NoError)
        return err;
    if (in->GetPosition() - info.DataOffset != info.DataSize)
        return kSvgErr_ComponentSizeMismatch;
    if (!AssertFormatTag(in, info.Name, false))
        return kSvgErr_ComponentClosingTagFormat;
    return kSvgErr_NoError;
}

SavegameError ReadAll(PStream in, SavegameVersion svg_version, const PreservedParams &pp, RestoredData &r_data)
{
    // Prepare a helper struct we will be passing to the block reading proc
    SvgCmpReadHelper hlp(svg_version, pp, r_data);
    GenerateHandlersMap(hlp.Handlers);

    size_t idx = 0;
    if (!AssertFormatTag(in, ComponentListTag, true))
        return kSvgErr_ComponentListOpeningTagFormat;
    do
    {
        // Look out for the end of the component list:
        // this is the only way how this function ends with success
        size_t off = in->GetPosition();
        if (AssertFormatTag(in, ComponentListTag, false))
            return kSvgErr_NoError;
        // If the list's end was not detected, then seek back and continue reading
        in->Seek(off, kSeekBegin);

        ComponentInfo info;
        SavegameError err = ReadComponent(in, hlp, info);
        if (err != kSvgErr_NoError)
        {
            Debug::Printf(kDbgMsg_Error, "ERROR: failed to read savegame component: index = %d, type = %s, version = %i, at offset = %u",
                idx, info.Name.IsEmpty() ? "unknown" : info.Name.GetCStr(), info.Version, info.Offset);
            return err;
        }
        update_polled_stuff_if_runtime();
        idx++;
    }
    while (!in->EOS());
    return kSvgErr_ComponentListClosingTagMissing;
}

SavegameError WriteComponent(PStream out, ComponentHandler &hdlr)
{
    WriteFormatTag(out, hdlr.Name, true);
    out->WriteInt32(hdlr.Version);
    size_t ref_pos = out->GetPosition();
    out->WriteInt32(0); // size
    SavegameError err = hdlr.Serialize(out);
    size_t end_pos = out->GetPosition();
    out->Seek(ref_pos, kSeekBegin);
    out->WriteInt32(end_pos - ref_pos - sizeof(int32_t)); // size of serialized component data
    out->Seek(end_pos, kSeekBegin);
    if (err == kSvgErr_NoError)
        WriteFormatTag(out, hdlr.Name, false);
    return err;
}

SavegameError WriteAllCommon(PStream out)
{
    WriteFormatTag(out, ComponentListTag, true);
    for (int type = 0; !ComponentHandlers[type].Name.IsEmpty(); ++type)
    {
        SavegameError err = WriteComponent(out, ComponentHandlers[type]);
        if (err != kSvgErr_NoError)
        {
            Debug::Printf(kDbgMsg_Error, "ERROR: failed to write savegame component: type = %s", ComponentHandlers[type].Name.GetCStr());
            return err;
        }
        update_polled_stuff_if_runtime();
    }
    WriteFormatTag(out, ComponentListTag, false);
    return kSvgErr_NoError;
}

} // namespace SavegameBlocks
} // namespace Engine
} // namespace AGS
