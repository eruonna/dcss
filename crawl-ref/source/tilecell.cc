#include "AppHdr.h"

#include "tilecell.h"

#include "cloud.h"
#include "coord.h"
#include "coordit.h"
#include "env.h"
#include "terrain.h"
#include "tiledef-dngn.h"

void packed_cell::clear()
{
    num_dngn_overlay = 0;
    fg = 0;
    bg = 0;
    cloud = 0;

    flv.floor_idx = 0;
    flv.wall_idx = 0;
    flv.feat_idx = 0;
    flv.floor = 0;
    flv.wall = 0;
    flv.feat = 0;
    flv.special = 0;

    is_bloody        = false;
    is_silenced      = false;
    is_suppressed    = false;
    halo             = HALO_NONE;
    is_moldy         = false;
    glowing_mold     = false;
    is_sanctuary     = false;
    is_liquefied     = false;
    mangrove_water = false;
    orb_glow         = 0;
    blood_rotation   = 0;
    old_blood        = false;
    travel_trail     = 0;
    quad_glow        = 0;
    disjunct         = 0;
    heat_aura        = 0;
}

bool packed_cell::operator ==(const packed_cell &other) const
{
    if (fg != other.fg) return false;
    if (bg != other.bg) return false;
    if (cloud != other.cloud) return false;

    if (is_bloody != other.is_bloody) return false;
    if (is_silenced != other.is_silenced) return false;
    if (is_suppressed != other.is_suppressed) return false;
    if (halo != other.halo) return false;
    if (is_moldy != other.is_moldy) return false;
    if (glowing_mold != other.glowing_mold) return false;
    if (is_sanctuary != other.is_sanctuary) return false;
    if (is_liquefied != other.is_liquefied) return false;
    if (mangrove_water != other.mangrove_water) return false;
    if (orb_glow != other.orb_glow) return false;
    if (blood_rotation != other.blood_rotation) return false;
    if (old_blood != other.old_blood) return false;
    if (travel_trail != other.travel_trail) return false;
    if (quad_glow != other.quad_glow) return false;
    if (disjunct != other.disjunct) return false;
    if (heat_aura != other.heat_aura) return false;

    if (num_dngn_overlay != other.num_dngn_overlay) return false;
    for (int i = 0; i < num_dngn_overlay; ++i)
        if (dngn_overlay[i] != other.dngn_overlay[i]) return false;
    return true;
}

enum wave_type
{
    WV_NONE = 0,
    WV_SHALLOW,
    WV_DEEP,
};

static wave_type _get_wave_type(bool shallow)
{
    return (shallow ? WV_SHALLOW : WV_DEEP);
}

static void _add_overlay(int tileidx, packed_cell *cell)
{
    cell->dngn_overlay[cell->num_dngn_overlay++] = tileidx;
}

typedef bool (*map_predicate) (const coord_def&);

static coord_def overlay_directions[] = {
    coord_def(0, -1),
    coord_def(1, 0),
    coord_def(0, 1),
    coord_def(-1, 0),
    coord_def(-1, -1),
    coord_def(1, -1),
    coord_def(1, 1),
    coord_def(-1, 1)
};

static void _add_directional_overlays(const coord_def& gc, packed_cell* cell,
                                      tileidx_t tile, map_predicate pred,
                                      uint8_t tile_mask = 0xFF)
{
    uint8_t dir_mask = 0;

    for (int i = 0; i < 8; ++i)
    {
        if (!pred(gc + overlay_directions[i]))
            continue;

        if (i > 3)
        {
            // Don't overlay corners if there's an overlay for one of the sides
            if (dir_mask & (1 << (i - 4)))
                continue;
            if (dir_mask & (1 << ((i - 1) % 4)))
                continue;
        }

        dir_mask |= 1 << i;
    }

    tileidx_t tileidx = tile;

    for (int i = 0; i < 8; ++i)
    {
        if ((tile_mask & (1 << i)) == 0)
            continue;

        if (dir_mask & (1 << i))
            _add_overlay(tileidx, cell);

        tileidx++;
    }
}

static void _pack_shoal_waves(const coord_def &gc, packed_cell *cell)
{
    // Add wave tiles on floor adjacent to shallow water.
    const dungeon_feature_type feat = env.map_knowledge(gc).feat();
    const bool feat_has_ink = (cloud_type_at(coord_def(gc)) == CLOUD_INK);

    if (feat == DNGN_DEEP_WATER && feat_has_ink)
    {
        _add_overlay(TILE_WAVE_INK_FULL, cell);
        return;
    }

    if (feat != DNGN_FLOOR && feat != DNGN_UNDISCOVERED_TRAP
        && feat != DNGN_SHALLOW_WATER && feat != DNGN_DEEP_WATER)
    {
        return;
    }

    const bool ink_only = (feat == DNGN_DEEP_WATER);

    wave_type north = WV_NONE, south = WV_NONE,
              east = WV_NONE, west = WV_NONE,
              ne = WV_NONE, nw = WV_NONE,
              se = WV_NONE, sw = WV_NONE;

    bool inkn = false, inks = false, inke = false, inkw = false,
         inkne = false, inknw = false, inkse = false, inksw = false;

    for (radius_iterator ri(gc, 1, true, false, true); ri; ++ri)
    {
        if (!env.map_knowledge(*ri).seen() && !env.map_knowledge(*ri).mapped())
            continue;

        const bool ink = (cloud_type_at(coord_def(*ri)) == CLOUD_INK);

        bool shallow = false;
        if (env.map_knowledge(*ri).feat() == DNGN_SHALLOW_WATER)
        {
            // Adjacent shallow water is only interesting for
            // floor cells.
            if (!ink && feat == DNGN_SHALLOW_WATER)
                continue;

            shallow = true;
        }
        else if (env.map_knowledge(*ri).feat() != DNGN_DEEP_WATER)
            continue;

        if (!ink_only)
        {
            if (ri->x == gc.x) // orthogonals
            {
                if (ri->y < gc.y)
                    north = _get_wave_type(shallow);
                else
                    south = _get_wave_type(shallow);
            }
            else if (ri->y == gc.y)
            {
                if (ri->x < gc.x)
                    west = _get_wave_type(shallow);
                else
                    east = _get_wave_type(shallow);
            }
            else // diagonals
            {
                if (ri->x < gc.x)
                {
                    if (ri->y < gc.y)
                        nw = _get_wave_type(shallow);
                    else
                        sw = _get_wave_type(shallow);
                }
                else
                {
                    if (ri->y < gc.y)
                        ne = _get_wave_type(shallow);
                    else
                        se = _get_wave_type(shallow);
                }
            }
        }
        if (!feat_has_ink && ink)
        {
            if (ri->x == gc.x) // orthogonals
            {
                if (ri->y < gc.y)
                    inkn = true;
                else
                    inks = true;
            }
            else if (ri->y == gc.y)
            {
                if (ri->x < gc.x)
                    inkw = true;
                else
                    inke = true;
            }
            else // diagonals
            {
                if (ri->x < gc.x)
                {
                    if (ri->y < gc.y)
                        inknw = true;
                    else
                        inksw = true;
                }
                else
                {
                    if (ri->y < gc.y)
                        inkne = true;
                    else
                        inkse = true;
                }
            }
        }
    }

    if (!ink_only)
    {
        // First check for shallow water.
        if (north == WV_SHALLOW)
            _add_overlay(TILE_WAVE_N, cell);
        if (south == WV_SHALLOW)
            _add_overlay(TILE_WAVE_S, cell);
        if (east == WV_SHALLOW)
            _add_overlay(TILE_WAVE_E, cell);
        if (west == WV_SHALLOW)
            _add_overlay(TILE_WAVE_W, cell);

        // Then check for deep water, overwriting shallow
        // corner waves, if necessary.

        // TILE_WAVE_DEEP_* is transition tile to floor;
        // deep water has another one for shallow water in TILE_WAVE_DEEP_* + 1
        int mod = (feat == DNGN_SHALLOW_WATER) ? 1 : 0;

        if (north == WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_N + mod, cell);
        if (south == WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_S + mod, cell);
        if (east == WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_E + mod, cell);
        if (west == WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_W + mod, cell);

        if (ne == WV_SHALLOW && !north && !east)
            _add_overlay(TILE_WAVE_CORNER_NE, cell);
        else if (ne == WV_DEEP && north != WV_DEEP && east != WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_CORNER_NE + mod, cell);
        if (nw == WV_SHALLOW && !north && !west)
            _add_overlay(TILE_WAVE_CORNER_NW, cell);
        else if (nw == WV_DEEP && north != WV_DEEP && west != WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_CORNER_NW + mod, cell);
        if (se == WV_SHALLOW && !south && !east)
            _add_overlay(TILE_WAVE_CORNER_SE, cell);
        else if (se == WV_DEEP && south != WV_DEEP && east != WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_CORNER_SE + mod, cell);
        if (sw == WV_SHALLOW && !south && !west)
            _add_overlay(TILE_WAVE_CORNER_SW, cell);
        else if (sw == WV_DEEP && south != WV_DEEP && west != WV_DEEP)
            _add_overlay(TILE_WAVE_DEEP_CORNER_SW + mod, cell);
    }

    // Overlay with ink sheen, if necessary.
    if (feat_has_ink)
        _add_overlay(TILE_WAVE_INK_FULL, cell);
    else
    {
        if (inkn)
            _add_overlay(TILE_WAVE_INK_N, cell);
        if (inks)
            _add_overlay(TILE_WAVE_INK_S, cell);
        if (inke)
            _add_overlay(TILE_WAVE_INK_E, cell);
        if (inkw)
            _add_overlay(TILE_WAVE_INK_W, cell);
        if (inkne || inkn || inke)
            _add_overlay(TILE_WAVE_INK_CORNER_NE, cell);
        if (inknw || inkn || inkw)
            _add_overlay(TILE_WAVE_INK_CORNER_NW, cell);
        if (inkse || inks || inke)
            _add_overlay(TILE_WAVE_INK_CORNER_SE, cell);
        if (inksw || inks || inkw)
            _add_overlay(TILE_WAVE_INK_CORNER_SW, cell);
    }
}

static dungeon_feature_type _safe_feat(coord_def gc)
{
    if (!map_bounds(gc))
        return DNGN_UNSEEN;

    return (env.map_knowledge(gc).feat());
}

static bool _is_seen_land(coord_def gc)
{
    const dungeon_feature_type feat = _safe_feat(gc);

    return (feat != DNGN_UNSEEN && !feat_is_water(feat) && !feat_is_lava(feat));
}

static bool _is_seen_shallow(coord_def gc)
{
    const dungeon_feature_type feat = _safe_feat(gc);

    return (feat == DNGN_SHALLOW_WATER || feat == DNGN_MANGROVE);
}

static void _pack_default_waves(const coord_def &gc, packed_cell *cell)
{
    // Any tile on water with an adjacent solid tile will get an extra
    // bit of shoreline.
    dungeon_feature_type feat = env.map_knowledge(gc).feat();

    // Treat trees in Swamp as though they were shallow water.
    if (cell->mangrove_water && feat == DNGN_TREE)
        feat = DNGN_SHALLOW_WATER;

    if (!feat_is_water(feat) && !feat_is_lava(feat))
        return;

    if (feat == DNGN_DEEP_WATER && !env.grid_colours(gc))
    {
        if (_is_seen_shallow(coord_def(gc.x, gc.y - 1)))
            _add_overlay(TILE_DNGN_WAVE_N, cell);
        if (_is_seen_shallow(coord_def(gc.x + 1, gc.y - 1)))
            _add_overlay(TILE_DNGN_WAVE_NE, cell);
        if (_is_seen_shallow(coord_def(gc.x + 1, gc.y)))
            _add_overlay(TILE_DNGN_WAVE_E, cell);
        if (_is_seen_shallow(coord_def(gc.x + 1, gc.y + 1)))
            _add_overlay(TILE_DNGN_WAVE_SE, cell);
        if (_is_seen_shallow(coord_def(gc.x, gc.y + 1)))
            _add_overlay(TILE_DNGN_WAVE_S, cell);
        if (_is_seen_shallow(coord_def(gc.x - 1, gc.y + 1)))
            _add_overlay(TILE_DNGN_WAVE_SW, cell);
        if (_is_seen_shallow(coord_def(gc.x - 1, gc.y)))
            _add_overlay(TILE_DNGN_WAVE_W, cell);
        if (_is_seen_shallow(coord_def(gc.x - 1, gc.y - 1)))
            _add_overlay(TILE_DNGN_WAVE_NW, cell);
    }

    // Sewer water
    if (feat == DNGN_DEEP_WATER && env.grid_colours(gc) == GREEN)
    {
        if (_is_seen_shallow(coord_def(gc.x, gc.y - 1)))
            _add_overlay(TILE_MURKY_WAVE_N, cell);
        if (_is_seen_shallow(coord_def(gc.x + 1, gc.y - 1)))
            _add_overlay(TILE_MURKY_WAVE_NE, cell);
        if (_is_seen_shallow(coord_def(gc.x + 1, gc.y)))
            _add_overlay(TILE_MURKY_WAVE_E, cell);
        if (_is_seen_shallow(coord_def(gc.x + 1, gc.y + 1)))
            _add_overlay(TILE_MURKY_WAVE_SE, cell);
        if (_is_seen_shallow(coord_def(gc.x, gc.y + 1)))
            _add_overlay(TILE_MURKY_WAVE_S, cell);
        if (_is_seen_shallow(coord_def(gc.x - 1, gc.y + 1)))
            _add_overlay(TILE_MURKY_WAVE_SW, cell);
        if (_is_seen_shallow(coord_def(gc.x - 1, gc.y)))
            _add_overlay(TILE_MURKY_WAVE_W, cell);
        if (_is_seen_shallow(coord_def(gc.x - 1, gc.y - 1)))
            _add_overlay(TILE_MURKY_WAVE_NW, cell);
    }

    bool north = _is_seen_land(coord_def(gc.x, gc.y - 1));
    bool west  = _is_seen_land(coord_def(gc.x - 1, gc.y));
    bool east  = _is_seen_land(coord_def(gc.x + 1, gc.y));

    if (north || west || east && (!env.grid_colours(gc) || env.grid_colours(gc) == LIGHTGREEN))
    {
        if (north)
            _add_overlay(TILE_SHORE_N, cell);
        if (west)
            _add_overlay(TILE_SHORE_W, cell);
        if (east)
            _add_overlay(TILE_SHORE_E, cell);
        if (north && west)
            _add_overlay(TILE_SHORE_NW, cell);
        if (north && east)
            _add_overlay(TILE_SHORE_NE, cell);
    }
}

static bool _is_seen_wall(coord_def gc)
{
    const dungeon_feature_type feat = _safe_feat(gc);
    return (feat != DNGN_UNSEEN && feat <= DNGN_MAXWALL
            && feat != DNGN_MANGROVE);
}

static void _pack_wall_shadows(const coord_def &gc, packed_cell *cell)
{
    if (_is_seen_wall(gc))
        return;

    if (_is_seen_wall(coord_def(gc.x - 1, gc.y)))
        _add_overlay(TILE_DNGN_WALL_SHADOW_W, cell);
    if (_is_seen_wall(coord_def(gc.x - 1, gc.y - 1)))
        _add_overlay(TILE_DNGN_WALL_SHADOW_NW, cell);
    if (_is_seen_wall(coord_def(gc.x, gc.y - 1)))
        _add_overlay(TILE_DNGN_WALL_SHADOW_N, cell);
    if (_is_seen_wall(coord_def(gc.x + 1, gc.y - 1)))
        _add_overlay(TILE_DNGN_WALL_SHADOW_NE, cell);
    if (_is_seen_wall(coord_def(gc.x + 1, gc.y)))
        _add_overlay(TILE_DNGN_WALL_SHADOW_E, cell);
}

static bool _is_seen_slimy_wall(const coord_def& gc)
{
    const dungeon_feature_type feat = _safe_feat(gc);

    return feat == DNGN_SLIMY_WALL;
}

void pack_cell_overlays(const coord_def &gc, packed_cell *cell)
{
    if (env.map_knowledge(gc).feat() == DNGN_UNSEEN)
        return; // Don't put overlays on unseen tiles

    if (player_in_branch(BRANCH_SHOALS))
        _pack_shoal_waves(gc, cell);
    else
        _pack_default_waves(gc, cell);

    if (player_in_branch(BRANCH_SLIME_PITS) &&
        env.map_knowledge(gc).feat() != DNGN_SLIMY_WALL)
    {
        _add_directional_overlays(gc, cell, TILE_SLIME_OVERLAY, _is_seen_slimy_wall);
    }
    else
        _pack_wall_shadows(gc, cell);
}
