.. _renderer-views:

==========
Data views
==========

.. container:: lede

   Every picture here is produced by tools/gen_previews.py from the current build, so nothing on the page can be stale: the command that made each image is printed above it. Each note gives the address of the rule its panel illustrates.

.. tab-set::

   .. tab-item:: Views

      .. rubric:: Pollution

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --view 6

      .. thumbnail:: img/preview/view-pollution.gif
         :group: renderer-views
         :alt: Pollution
         :show_caption: True

         --view 6, whole map at maximum zoom, 1:1 · 4224x2468

      .. rubric:: Power

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --view 9

      .. thumbnail:: img/preview/view-power.gif
         :group: renderer-views
         :alt: Power
         :show_caption: True

         --view 9, whole map at maximum zoom, 1:1 · 4224x2468

      .. rubric:: Traffic

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --view 1

      .. thumbnail:: img/preview/view-traffic.gif
         :group: renderer-views
         :alt: Traffic
         :show_caption: True

         --view 1, whole map at maximum zoom, 1:1 · 4224x2468

      .. rubric:: Underground — Bayview

      .. code-block:: console

         arcology --soft assets 'Bayview' out.png --zoom 32 --underground

      .. thumbnail:: img/preview/view-under.gif
         :group: renderer-views
         :alt: Underground - Bayview
         :show_caption: True

         --underground, whole map at maximum zoom, 1:1 · 4224x2468

      .. rubric:: Underground — Manhattan

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --underground

      .. thumbnail:: img/preview/view-under-mh.gif
         :group: renderer-views
         :alt: Underground - Manhattan
         :show_caption: True

         --underground, whole map at maximum zoom, 1:1 · 4224x2468
