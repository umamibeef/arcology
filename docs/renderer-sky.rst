.. _renderer-sky:

=======
The sky
=======

.. container:: eyebrow

   The enhanced world · the renderer

.. container:: lede

   The clouds over the mesh, and their shadows on everything under them. The air pass (``air.frag``) marches each pixel's ray through a layer of cloud, as Nubis marches one (Schneider, 2015). The clouds' place and size come from one field of cloud, as Nubis takes them from its weather map. The same pass shades the ground and the art under them, so a shadow always lies under its own cloud.

In the game
-----------

Each picture is a frame of the game, made by ``tools/gen_previews.py`` from the current build with the command above it. A headless ``--shot`` draws the live window's frame with no interface on it. ``--hour`` sets the time of day for one run, as the View menu does.

.. tab-set::

   .. tab-item:: Clouds

      .. code-block:: console

         arcology oakland --shot out.png --zoom 0.5

      .. thumbnail:: img/preview/sky-clouds.png
         :group: renderer-sky
         :alt: Clouds
         :show_caption: True

         Oakland at half the art's size, under a cloud cover of 41 per cent
   .. tab-item:: Midday

      .. code-block:: console

         arcology bayview --shot out.png --scroll 1750,1400

      .. thumbnail:: img/preview/sky-midday.png
         :group: renderer-sky
         :alt: Midday
         :show_caption: True

         Bayview at the fitted hour, under a clear sky
   .. tab-item:: Dusk

      .. code-block:: console

         arcology bayview --shot out.png --scroll 1750,1400 --hour 17.6

      .. thumbnail:: img/preview/sky-dusk.png
         :group: renderer-sky
         :alt: Dusk
         :show_caption: True

         the same frame at dusk
   .. tab-item:: Night

      .. code-block:: console

         arcology bayview --shot out.png --scroll 1750,1400 --hour 22

      .. thumbnail:: img/preview/sky-night.png
         :group: renderer-sky
         :alt: Night
         :show_caption: True

         the same frame at night

The field
---------

.. rubric:: ``clouds_field`` · ``SKY_FIELD_N`` · ``cloud_depth``

The field is value noise of four octaves, baked once into a texture of ``SKY_FIELD_N`` texels a side that repeats with no seam (``src/render/gpu/clouds.c``). The pass reads it at two scales, the second at six tenths of the first, and takes the mean of the two. The wind carries each scale its own way, the second slower and on a wind turned a little, so the clouds do not slide as one sheet.

The bake also measures the mean and the spread of that mean over the plane. The pass equalizes the field by them, so a cover of 0.35 puts about 35% of the map under cloud. How deep a point lies in its cloud runs from 0 at the cloud's edge to 1 where the field is highest.

The clouds
----------

.. rubric:: ``cloud_at`` · ``cloud_noise`` · ``clouds_on``

The field says where the clouds are and how far into one a point lies. A noise volume gives each cloud its body in three dimensions, as Nubis shapes one. The noise is cut at one less that depth. So deep in a cloud most of the noise stays, and out at its reach only the noise's strongest lumps are left: the wisps. The field's outline lies ``sky_reach`` of the cover further out, so the wisps reach past the clouds' bodies and the share the clouds are seen to cover is the cover itself. A wisp keeps a small share of a cloud's thickness, so it shows and is seen through.

A cloud stands on a rounded base at ``sky_altitude`` levels, and rises taller toward its middle, up to ``sky_thickness`` levels. A finer noise wears its body, turned over low in the cloud, so the base frays and the top billows. The march lights each step by the sun through the cloud above it, with light scattered many times inside the cloud (Wrenninge, 2013) and two lobes of phase (Hillaire, 2016).

.. caution::

   The noise volume repeats every ``cloud_shape_m`` meters, and a cloud spans several repeats. Read on its own grid, the same lumps come back in every cloud. ``cloud_noise`` reads it twice, at two scales whose ratio is 1.618 and each turned by an angle that lines up with nothing, so the two repeats never meet within a view.

The clouds clear as the view closes in, between ``cloud_zoom_full`` and ``cloud_zoom_gone``. Close in, only the shadows are left. The clouds thin out past the map's edge, from ``sky_fade_from`` to ``sky_fade_to`` tiles beyond it, so they never stop on a line. They keep clear of a circle round the pointer on the screen, ``sky_cursor_hole`` tiles across at the view's zoom, with a soft rim. The pass clears it in its own pixels, so the circle sits on the cursor at any zoom, pitch and turn. The shadows stay.

The march at a lower resolution
-------------------------------

.. rubric:: ``air_clouds.frag`` · ``air_body.frag`` · ``cloud_res``

A cloud pass marches the clouds alone into a target of its own. Each of its pixels covers ``cloud_res`` of the frame's pixels a side, so at 2 it marches a quarter of the rays. The air pass then filters that target up to the frame and lays it over the frame. A cloud's ray starts on the camera's plane, at a place that the pixel alone gives. It does not read the frame's depth, so the filter needs no care at the edges of what lies under the clouds. The two shaders are one body, ``air_body.frag``, built with and without ``CLOUD_MARCH``. The shadows stay at the frame's full resolution, because they lie on the ground's edges.

The shadows
-----------

.. rubric:: ``cloud_shadow``

The pass finds the world point under each pixel from the frame's depth, and marches from it toward the sun through the layer. What the clouds let through takes the sun's share of the frame's light at that point. The ambient light stays, so a slope in shadow still reads as a slope. The ground, a zone's tint and the art are all shaded the same way, because the pass reads the frame they made together.

The hour
--------

.. rubric:: ``sun_clock`` · ``FIT_SUN`` · ``FIT_HOUR`` · ``lit3``

The View menu's *Time of day* sets the hour: dawn, morning, midday, afternoon, dusk or night, or any hour on its slider. The choice is kept between runs, and it sets ``sun_hour``. ``--hour H`` sets the hour for one run and does not keep it. Every light in the frame follows it. The sun runs a fixed arc, as on an equinox: it rises near six and sets near eighteen. The sun the light is fitted to (``FIT_SUN``) lies on the arc at ``FIT_HOUR``, the hour ``sun_hour`` holds by default. There the light is the fitted one exactly, so the default frame is the frame without a clock. The hour turns the sun about the arc's pole by fifteen degrees an hour, and the sun's height over the horizon gives the light:

* **The sun's color.** What reaches the ground through the air (``air_sun``), over what reaches it at the fitted hour. The air takes blue before red, so the sun warms as it sinks, and it goes out at the horizon.
* **The ambient.** The sky's light on a level surface (``air_multiple``), over its light at the fitted hour. At night, when the sky is dark, a dim blue still shows the city.
* **The exposure.** As an eye or a camera does, the frame takes in more of a dim hour's light: the fitted hour's brightness over the hour's, to the power 0.75, and at most 2.5. So dusk is golden and still dimmer than noon. The clouds face the sun, so their sun is held to its brightest channel at 1.1, and keeps its hue.
* **The art's tint.** The art carries the fitted hour's light. At any other hour the air pass tints it by that hour's light over the fitted hour's, on a level face. The mesh is lit by the hour itself (``lit3``).

The air is the one the frame is seen through, so the light, the haze and the clouds agree at every hour. The sun's shadow map, the clouds' light, the water's glint and the occlusion all read the same sun. A change of hour draws the shadow map again once. Below the horizon the shadow map is not drawn.

The sun's shadow
----------------

.. rubric:: ``sun_frame`` · ``sun_depth.vert`` · ``sun_seen``

The mesh shades itself from the one sun with a shadow map (Williams, 1978). A pass of depth alone draws the mesh as the sun sees it, into a square of ``sun_shadow_res`` texels a side. The square covers every chunk in view, widened by ``sun_shadow_margin``. Each fragment of the mesh then compares its own distance from the sun with the map's, in four taps that the device filters. What a shadow covers loses ``sun_shadow`` of the sun's light, and keeps the ambient light. A point moves out along its normal by a texel and a half before it is compared, and the depth pass adds a bias that grows with the slope. So a face does not shade itself.

The map is drawn again only when what it shows changes: the mesh, the sun or the level of detail. It is also drawn again when the view leaves the square, or closes in so far that the square's texels are coarse for it. A still, a panning or a turning view draws nothing for its shadows. The sprites neither cast nor take the sun's shadow, because their art carries its own light.

The light on the ground
-----------------------

.. rubric:: ``shore_field`` · ``ground_bumps`` · ``terrain_body.frag``

The ground takes its light from its real shape, and from no rule about hills or valleys.

* **The smoothed surface.** Each vertex carries the height field's smoothed gradient. On gentle ground the light falls on that surface, so a hill is lit as one rounded form. A face as steep as a cliff keeps its own plane, so its edge stays crisp.
* **The sky a tile sees.** In each of eight ways, the highest angle the land rises to within 16 tiles is the horizon that way. Over a horizon at angle *a*, a cosine-weighted sky keeps cos² *a* of its light. The mean over the eight ways scales the sky's light, so a valley is darker only by the land that stands over it. ``shore_field`` (``src/render/shore.c``) measures it into the field's fourth byte. After an edit it measures again only within 16 tiles of the tiles whose heights changed.
* **The large bumps.** A height is one a tile corner in whole levels, so the mesh cannot hold a rise a few tiles wide. The bumps tilt the light on open ground only, and nothing the game reads moves. They come from the repeating noise of the sky's field, at two widths turned to each other. A width fades out as it spans fewer than ten pixels, so the bumps never shimmer at a distance.

The occlusion
-------------

.. rubric:: ``air_ao.frag`` · ``occlusion`` · ``occlusion_here``

What stands round a point of the mesh hides part of the sky from it. The air pass finds that share as Alchemy does (McGuire et al., 2011). Sixteen taps on a spiral reach out ``ao_radius`` tiles round the pixel, and each tap is a point of the frame from its depth. A tap above the pixel's plane hides part of the sky, the more the nearer and the steeper it stands. Whatever drew depth hides the sky: the ground, a deck, a column, a prop and the buildings' art. Only the mesh takes the occlusion, because the art carries its own shading.

The share takes the sky's part of the frame's light away and leaves the sun's. So the foot of a building, the ground under a deck and a curb's corner darken, and a face in the sun stays lit.

The pass runs at one pixel for ``ao_res`` of the frame's a side. Each pixel of a four by four block turns the spiral by its own angle. The air pass filters the answer up over the same four by four block, and weighs each pixel by how near its depth is. So the turns even out, and a deck's occlusion does not bleed onto the ground beside it. The occlusion stops where its reach covers fewer than three pixels, so a view from far out pays nothing for it.

The water
---------

.. rubric:: ``water_surface`` · ``sky_toward`` · ``cam_ray``

The mesh's water shows only what reads from high above. Ripples, sparkle and caustics are smaller than a tile, so the surface has none.

* **The body.** The sun's light goes down through the water to the bed and comes back to the camera. Each level of water takes a share of each color, red the most. So the shallows show the sand as turquoise, and the deep shows only the dark blue that the water itself scatters. The depth is the tile's own, from the shore field. ``water_clarity`` divides the share each level takes.
* **The top.** Swells ``water_wave_tiles`` long move on the wind the clouds move on. They come from the repeating noise that gives the ground its bumps, and they fade out when they grow too small to see. Fresnel weighs the sky they reflect against the body, from the camera's real way (``cam_ray``). Looking straight down, the water shows its body. Toward the horizon it shows the sky. A broad glint follows the sun.
* **The surf.** Bands of surf run in toward the coast, broken in patches along it, with a steady line at the water's edge. Bands too fine to see fade to a faint mean. Calm water, a marina or a stream, has no swell and no surf.

The land's cover
----------------

.. rubric:: ``land_cover`` · ``cover_noise``

From above, open land reads as a mosaic of meadow, scrub, woodland and bare earth. Each patch is tens of tiles across, with soft edges. The repeating noise of the sky's field says where a patch would lie. It is read at ``cover_tiles`` tiles a cell and again turned at a smaller size, and it is evened by the mean and spread the bake measures. The land says what a place can hold:

* **Woodland** gathers in damp hollows and on gentle slopes, and thins with height and steepness.
* **Scrub** lies where the land is drier and the noise is low.
* **Bare earth** comes out only on dry, high ridges that face the sun.
* **Meadow** takes what is left.

Inside a wood the crowns show as a mottle about a tile wide. The mottle and the ground's fine grain fade out as they span too few pixels to see.

The cover
---------

The share of the sky under cloud is the cities' own cloud cover, ``weather2``, averaged over the world's cities. It is never less than ``sky_cover_least``, which is nought, so a city whose weather is clear has a clear sky. The app reads it each frame (``sky_cover``, ``src/app/frame.c``).

The numbers
-----------

Every number is in ``geo_defaults`` (``src/render/geo.c``). The Debug menu's *Shading* window moves them live, in sections: light and shadow, clouds, cloud edges, ground, water and cost.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Number
     - Default
     - What it is
   * - ``sky_cover_least``
     - 0
     - The least share of the sky under cloud.  The cities' own weather sets the rest
   * - ``sky_scale``
     - 24
     - The tiles the field's lowest octave spans: clouds several blocks across
   * - ``sky_speed``, ``sky_wind_x``, ``sky_wind_y``
     - 0.7, 0.8, 0.6
     - The wind's speed in tiles a second, about ten meters a second, and its direction
   * - ``sky_altitude``, ``sky_thickness``
     - 48, 16
     - The clouds' base, and their tallest above it, in levels
   * - ``sky_cursor_hole``
     - 6
     - The tiles kept clear round the pointer
   * - ``sky_fade_from``, ``sky_fade_to``
     - 0, 16
     - The tiles past the map's edge over which the clouds thin out
   * - ``sky_reach``
     - 0.37
     - The share of the cover past it that the clouds' wisps reach
   * - ``cloud_shape_m``, ``cloud_detail_m``, ``cloud_erode``
     - 320, 60, 0.2
     - The meters the shape noise and the detail repeat over, and how hard the detail wears the body
   * - ``cloud_ext``, ``cloud_light``
     - 0.04, 1.0
     - The clouds' extinction per meter at their thickest, and the sun's light on them, for the light scattered many times in a thick cloud
   * - ``ao_strength``, ``ao_radius``, ``ao_bias``, ``ao_res``
     - 1, 0.75, 0.1, 2
     - The occlusion's strength, its reach in tiles, the share of a tap's way it ignores, and the frame's pixels a side one of its pixels covers
   * - ``water_clarity``, ``water_wave``, ``water_wave_tiles``
     - 1, 0.15, 6
     - How clear the water is, and its swells' steepest slope and length in tiles
   * - ``cover``, ``cover_tiles``
     - 1, 40
     - How strongly the land's cover shows, and the tiles a cell of its noise spans
   * - ``ground_bump``, ``ground_bump_tiles``
     - 0.15, 5
     - The large bumps' steepest slope, and the width in tiles of the wider of their two sizes
   * - ``sun_hour``
     - 13.052
     - The hour of the day, from 0 to 24. The default is the hour of the fitted light
   * - ``sun_shadow``, ``sun_shadow_res``, ``sun_shadow_margin``
     - 0.85, 2048, 1.5
     - The share of the sun a shadow takes, the shadow map's texels a side, and how much wider than the view it covers
   * - ``cloud_res``
     - 2
     - The frame's pixels a side that one pixel of the clouds' march covers

The smog
--------

.. rubric:: ``smog_at`` · ``SMOG_TINT`` · ``XPLT``

The smog lies over the tiles the city pollutes, and nowhere else. Its density is the city's own pollution (``XPLT``, at half the map's resolution), and only heavy pollution shows: the share runs from nought at a fifth of the scale to its full at seven tenths. It thins with its height over the ground under the ray, over ``smog_h`` meters. So it hangs low over an industrial district and leaves the homes round it clear.

The air pass marches it on its own, in eight steps through the part of the ray under its top. The light from the ground passes it first and the air above it after. Its particles scatter a dirty yellow (``SMOG_TINT``), and they absorb more blue than red, as nitrogen dioxide does. The sun and the whole sky light it. Light scatters many times inside a thick haze, as in a cloud, so ``SMOG_MULTI`` stands for the light one scattering leaves out, and the haze is brighter than the ground under it.

The switch
----------

The clouds draw only over the mesh. The View menu's *Clouds* turns them on and off, and the choice is kept between runs. ``--disable clouds`` turns them off for one run. The View menu's *Air* turns the air on and off, and it is on unless turned off. ``--disable air`` turns it off for one run. With the air on, the air pass also draws the air itself: the haze, the smog and the light the air scatters.

With the clouds, the occlusion and the air all off, at the fitted hour, the air pass does not run. The frame is then the frame without a sky, to the pixel.
