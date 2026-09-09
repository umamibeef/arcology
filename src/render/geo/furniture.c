/*  furniture.c -- the street furniture: a pass of its own.  Everything that
 *  STANDS on a road or a railway rather than being one: the lamps and signs
 *  along a strip, a junction's signals and stop signs, a level crossing's
 *  crossbucks, gates, flashers and second-train signs.  Lifted out of the
 *  loft and the junction so it can be switched off as one: every entry
 *  point here returns at once while the pass is off, and the mesh is built
 *  without them.  Cars' own lights are the traffic's, not furniture:
 *  put_lamp_face stays public for them. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "geo/model.h"
#include "script.h"
#include "opt.h"
#include "project.h"

static int s_furniture = 1; /* on unless the app says otherwise (View > Street furniture, --no-furniture) */

void furniture_enable(int on)
{
    s_furniture = on ? 1 : 0;
}

int furniture_on(void)
{
    return s_furniture;
}

/*  A traffic signal for the approach from edge `e` of a road junction at
 *  tile (col, row): a pole at the driver's right-hand corner of the
 *  junction, a mast arm from its top out over the road, and a three-lamp
 *  head hanging from the arm's end, its lamps facing the approaching
 *  traffic.  The lamps carry the junction's phase in col.r and, in col.g,
 *  their group (north-south or east-west arms) and which lamp they are, for
 *  the cycle the shader runs. */
int put_signal(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h)
{
    if (!furniture_on())
        return 0;
    /*  Put at the tile's middle facing the driver the signal is for,
     *  with the junction's half width for its size: the model steps
     *  itself out to the mouth and across to the driver's own corner,
     *  and every measurement it does that by is a script's.  The lamps
     *  carry the junction's phase in col.r and, in col.g, their group --
     *  which pair of arms they belong to -- for the cycle the shader
     *  runs. */
    float phase = (float)((col * 7 + row * 13) % 8) / 8.0f;
    float group = (e == 0 || e == 2) ? 0.0f : 3.0f;
    return net_model_put(net_model_find("signal"), m, c, mask_bit, order,
                         (float)col + 0.5f, (float)row + 0.5f,
                         -ROAD_DU[e], -ROAD_DV[e], h, phase, group);
}

/*  A crossbuck at a rail crossing: a post with two crossed arms. */
static int put_crossbuck(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, int along_u)
{
    if (!furniture_on())
        return 0;
    return net_model_put(net_model_find("crossbuck"), m, c, mask_bit, order, x, y,
                         along_u ? 1.0f : 0.0f, along_u ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f);
}

/*  One lamp face, a small square of MAT_LAMP with lamp code `code`
 *  facing (fx, fy) at (x, y), `z` levels over the ground `g`. */
int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code)
{
    if (curves_hidden(MAT_LAMP))
        return 0; /* the overlay hides the signals with everything else */
    float lamp[3] = {phase, code, MAT_LAMP};
    float q[4][3], tri[3][3], ref[3] = {phase, phase, phase}, ref2[3] = {code, code, code};
    float wu = -fy * sz, wv = fx * sz, nrm[3] = {fx, fy, 0.0f};
    float ox = x + fx * 0.02f, oy = y + fy * 0.02f;
    q[0][0] = ox - wu;
    q[0][1] = oy - wv;
    q[0][2] = g + z - sz;
    q[1][0] = ox + wu;
    q[1][1] = oy + wv;
    q[1][2] = g + z - sz;
    q[2][0] = ox + wu;
    q[2][1] = oy + wv;
    q[2][2] = g + z + sz;
    q[3][0] = ox - wu;
    q[3][1] = oy - wv;
    q[3][2] = g + z + sz;
    memcpy(tri[0], q[0], sizeof tri[0]);
    memcpy(tri[1], q[1], sizeof tri[1]);
    memcpy(tri[2], q[2], sizeof tri[2]);
    if (code >= 12.5f && code < 13.5f)
    {
        /* a sign's face: u in col.r, v in the fraction of col.g, for its shape */
        ref[0]  = 0.0f;
        ref[1]  = 1.0f;
        ref[2]  = 1.0f;
        ref2[0] = 13.0f;
        ref2[1] = 13.0f;
        ref2[2] = 13.5f;
    }
    if (put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0) != 0)
        return -1;
    memcpy(tri[1], q[2], sizeof tri[1]);
    memcpy(tri[2], q[3], sizeof tri[2]);
    if (code >= 12.5f && code < 13.5f)
    {
        ref[0]  = 0.0f;
        ref[1]  = 1.0f;
        ref[2]  = 0.0f;
        ref2[0] = 13.0f;
        ref2[1] = 13.5f;
        ref2[2] = 13.5f;
    }
    return put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0);
}

/*  A wayside colour-light signal (spec 5.6): a mast, a head with a
 *  hood, and one steady aspect facing the approaching train, green on
 *  a block signal, red on an absolute one at a junction. */
int put_rail_signal(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int absolute, float s_along, int dir)
{
    if (!furniture_on())
        return 0;
    RRailSig *sig;
    if (net_model_put(net_model_find("rail_signal"), m, c, mask_bit, order, x, y, fx, fy, 0.0f, 0.0f, 0.0f) != 0)
        return -1;
    /* the aspect is the traffic's, lit by the block's occupancy each frame */
    if (m->n_rsigs + 1u > m->cap_rsigs)
    {
        uint32_t  nc = m->cap_rsigs ? m->cap_rsigs * 2u : 128u;
        RRailSig *ns = (RRailSig *)realloc(m->rsigs, nc * sizeof *ns);
        if (!ns)
            return -1;
        m->rsigs     = ns;
        m->cap_rsigs = nc;
    }
    sig           = &m->rsigs[m->n_rsigs++];
    sig->x        = x;
    sig->y        = y;
    sig->fx       = fx;
    sig->fy       = fy;
    sig->s        = s_along;
    sig->seg      = (int32_t)m->railnet.n_segs - 1;
    sig->dir      = dir;
    sig->absolute = absolute;
    return 0;
}

/*  A level crossing's protection on one approach (spec 3.15): the
 *  mast at the driver's right with the crossbuck, the "2 TRACKS"
 *  plaque, a pair of flashers below it, and the gate on its own
 *  counterweighted post beside the mast, its striped arm down across
 *  the approach lane with the flashers lit while a train stands within
 *  three tiles of the crossing, raised otherwise; and a second-train
 *  sign facing each sidewalk.  (fx, fy) is the direction of travel on
 *  this approach; the face turns to meet it. */
int put_gate(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, int along_u)
{
    if (!furniture_on())
        return 0;
    /*  The standing parts of a crossing's protection on one approach
     *  (spec 3.15, 6.1): the mast at the driver's right with the
     *  crossbuck, the "2 TRACKS" plaque, the flasher bar and the base
     *  junction box, and the gate mechanism's case beside it on the
     *  road side.  The lamps and the arm are the traffic's, rebuilt each
     *  frame with the trains' positions.  (fx, fy) is the direction of
     *  travel on this approach. */
    if (put_crossbuck(m, c, mask_bit, order, x, y, along_u) != 0)
        return -1;
    /*  The mast's standing parts, from the model: the base junction box,
     *  the flasher bar, the plaque and the mechanism's case.  The lamps
     *  and the arm are the traffic's, rebuilt each frame. */
    return net_model_put(net_model_find("gate"), m, c, mask_bit, order, x, y, -fx, -fy, 0.0f, 0.0f, 0.0f);
}

/*  A second-train sign: a post with a yellow diamond facing the
 *  sidewalk it stands at the end of, one at each of the crossing's
 *  four sidewalk corners (spec 3.15, tracks >= 2). */
int put_second_train_sign(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy)
{
    if (!furniture_on())
        return 0;
    return net_model_put(net_model_find("second_train"), m, c, mask_bit, order, x, y, fx, fy, 0.0f, 0.0f, 0.0f);
}

/*  ==================================================================
 *  Lofting
 *
 *  The ribbon swept from the stations, with three jobs of its own beside
 *  it: the furniture that stands next to the line, the corridor surface
 *  notched under it, and the overlay drawn over it for looking at.
 *  ================================================================== */
/*  Signals, lighting, and everything else that stands BESIDE the line
 *  rather than being part of the ribbon. */
int loft_furniture(Loft *x)
{
    if (!furniture_on())
        return 0;
    return net_family_has(x->d->fam, NH_FURNITURE) ? net_family_furniture(x->d->fam, x) : 0;
}

int put_stop_sign(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h)
{
    if (!furniture_on())
        return 0;
    /*  As the signal: the tile's middle and the way the driver faces,
     *  and the model stands itself back from the mouth on that driver's
     *  own side of the road. */
    return net_model_put(net_model_find("stop_sign"), m, c, mask_bit, order,
                         (float)col + 0.5f, (float)row + 0.5f,
                         -ROAD_DU[e], -ROAD_DV[e], h, 0.0f, 0.0f);
}

/*  A junction of a family: the box, its sidewalk corners rounded as
 *  curb returns where two arms meet and square where a side is free,
 *  the outline along a free side, and for a road a signal on every arm. */
/* ---- stage three: a junction takes its shape from its arms ------------- */

/*  The outline of the junction at (col,row), and how far along each arm its
 *  strip should start.  Every arm leaves at its own angle with its own half
 *  width, so its two edges are two lines through the junction.  Where one
 *  arm's left edge meets the next arm's right edge is a corner of the
 *  junction; between two corners an arm has its mouth, cut square across
 *  the arm at whichever of its corners reaches further out.  Four arms
 *  leaving along the tile's own axes give back exactly the square, so
 *  nothing changes where nothing is wrong.  Returns the number
 *  of points written, or 0 if there is nothing to draw. */

/*  A bar between two points in the air, `w` half its width across the
 *  face it turns to (fx, fy) and `d` half its depth: that face and its
 *  top, the far side being the post's and unseen.  `code` is the aspect
 *  the material paints along it -- a gate arm's red and white stripes
 *  live in the channel MAT_LAMP reads, and a hair of a bar, or the code
 *  in the wrong channel, makes a white thread no zoom can show. */
int put_bar(RMesh *m, const RCity *c, uint8_t mask_bit, float order,
            float x0, float y0, float z0, float x1, float y1, float z1,
            float fx, float fy, float w, float d, float mat, float code, float phase)
{
    float col[3] = {phase, code, mat}, ref[3] = {0.0f, 0.0f, 0.0f}, ref2[3] = {code, code, code};
    float a0[3] = {x0 - fx * w, y0 - fy * w, z0 + d}, a1[3] = {x1 - fx * w, y1 - fy * w, z1 + d};
    float b0[3] = {x0 - fx * w, y0 - fy * w, z0 - d}, b1[3] = {x1 - fx * w, y1 - fy * w, z1 - d};
    float c0[3] = {x0 + fx * w, y0 + fy * w, z0 + d}, c1[3] = {x1 + fx * w, y1 + fy * w, z1 + d};
    float nrm[3] = {fx, fy, 0.0f}, up[3] = {0.0f, 0.0f, 1.0f}, t3[3][3];
    ARM_TRI(a0, a1, b1, nrm);
    ARM_TRI(a0, b1, b0, nrm);
    ARM_TRI(a0, c0, c1, up);
    ARM_TRI(a0, c1, a1, up);
    return 0;
}

/*  The gates' moving parts on one approach: the flashers, lit while the
 *  arm is off its rest, and the striped arm swung about the mechanism's
 *  shaft.  Where the shaft is, how the arm rises as it swings and which
 *  lamps are lit are arc.rules.gate_arm's, drawn from the bar and the
 *  models: nothing of the gate's movement is decided here. */
int furniture_gate_state(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x, float y, float fx, float fy, float angle, float arm_len, float time)
{
    ScriptProp at;
    if (!s_furniture)
        return 0; /* the pass off: no gate to swing */
    (void)time;
    at.col = (int)floorf(x), at.row = (int)floorf(y), at.arm = -1, at.links = 0;
    at.x = x, at.y = y;
    at.z = surface_at_world(c, mask_bit, x, y);
    at.fx = -fx, at.fy = -fy;
    at.size = 0.0f;
    at.phase = (float)(((int)(x * 3.0f) + (int)(y * 5.0f)) & 7) / 8.0f;
    at.angle = angle, at.len = arm_len;
    script_emit_open(m, c, mask_bit, order);
    script_rule_prop("gate_arm", &at);
    script_emit_close();
    return 0;
}

/*  A rail signal's aspect, red or green, lit on its head: the traffic
 *  decides which (spec 5.6), the furniture draws it. */
int furniture_rail_aspect(RMesh *m, const RCity *c, uint8_t mask_bit, const RRailSig *sg2, float g, int red)
{
    int32_t tc = (int32_t)floorf(sg2->x), tr2 = (int32_t)floorf(sg2->y);
    if (!s_furniture)
        return 0;
    (void)g;
    return net_model_put_on(net_model_find(red ? "rail_aspect_red" : "rail_aspect_green"),
                            m, c, mask_bit, tile_order(c, tc, tr2, mask_bit),
                            sg2->x, sg2->y, sg2->fx, sg2->fy, 0.0f, 0.0f, 0.0f, g, g, 1);
}
