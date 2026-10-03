/*  names.c: the names the interface shows for the tools.  Both builds of
 *  the interface use these, with Dear ImGui and without it. */
#include "ui.h"

const char *const RUI_TOOL_NAME[RUI_N_TOOLS] = {
    "Bulldozer", "Landscape", "Dispatch", "Power", "Water", "City Bonus", "Roads", "Rail", "Ports", "Residential", "Commercial", "Industrial", "Education", "Health & Safety", "Recreation", "Place Sign", "Query", "Center Display", "Zoom Out", "Zoom In", "Zone Demand", "Rotate CCW", "Rotate CW", "Map", "Population", "Neighbors", "Graphs", "Industry", "Budget", "Buildings", "Signs", "Roads & Trees", "Zones", "Under-view"};

/*  The sub-tools of each map tool, as the original names them in its
 *  pointer table at A5+0x28, twelve a tool.  The spelling is the
 *  original's.  "Power Plant" and "Arcologies" are headings in the
 *  original's menu, and they have no price. */
const char *const RUI_SUB_NAME[RUI_N_MAP_TOOLS][RUI_N_SUB] = {
    {"Demolish/Clear", "Level Terrain", "Raise Terrain", "Lower Terrain", "De-zone"},
    {"Trees", "Water"},
    {"Dispatch Police", "Dispatch Firefighters", "Dispatch Military"},
    {"Power Lines", "Power Plant", "Coal Power", "Hydro Electric", "Oil Power", "Gas Power", "Nuclear", "Wind Power", "Solar Power", "Microwave", "Fusion"},
    {"Pipes", "Water Pump", "Water Tower", "Treatment", "Desalinization"},
    {"Mayors House", "City Hall", "Statue", "Braun Llama Dome", "Arcologies", "Plymoth Arco", "Forest Arco", "Darco", "Launch Arco"},
    {"Road", "Highway", "Tunnel", "Onramp", "Bus Depot"},
    {"Rail", "Subway", "Rail Depot", "Sub Station", "Sub<-->Rail"},
    {"Seaport", "Airport"},
    {"Light Residential", "Dense Residential"},
    {"Light Commercial", "Dense Commercial"},
    {"Light Industrial", "Dense Industrial"},
    {"School", "College", "Library", "Museum"},
    {"Police", "Fire Station", "Hospital", "Prison"},
    {"Small Park", "Big Park", "Zoo", "Stadium", "Marina"},
    {"Sign Tool"},
    {"Query Tool"},
    {"Centering Tool"}
};
