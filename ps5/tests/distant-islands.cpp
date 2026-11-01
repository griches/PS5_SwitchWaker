#include "pc/pc_distant_islands.h"
#include <cassert>
#include <limits>
int main() {
    assert(!pc_island_optional_request(40000,40000));
    assert(!pc_island_optional_request(40000,39999));
    assert(pc_island_optional_request(40000,40001));
    assert(pc_island_optional_request(40000,59999)); // unload band is not native request range
    assert(pc_island_optional_request(40000,std::numeric_limits<float>::quiet_NaN()));
    PcIslandBudget budget;
    PcIslandCandidate islands[] = {
        {40000,30000,false,true,false,false}, // original island reserves a slot
        {40000,90000,false,false,false,false},
        {40000,70000,false,false,false,false}, // nearest extra
        {40000,200000,false,false,false,false}, // beyond HD horizon
    };
    budget.limit=2;
    budget.select(islands,4,true,true);
    assert(!islands[0].wanted && !islands[1].wanted && islands[2].wanted && !islands[3].wanted);
    islands[2].extended=true;
    islands[2].distance=50000;
    budget.select(islands,4,true,true);
    assert(islands[2].wanted); // no pop when sailing into native range
    assert(!islands[1].wanted); // both near islands consume the budget
    budget.select(islands,4,false,true);
    for (auto& island:islands) assert(!island.wanted); // Off restores base distances
    budget.select(islands,4,true,false);
    for (auto& island:islands) assert(!island.wanted); // no player, no speculative streaming
    islands[2].loaded=true;
    islands[2].spilled=true;
    budget.select(islands,4,true,true);
    assert(budget.limit==0); // spill tightens the budget conservatively
    budget.allocationFailed();
    assert(budget.limit==0);
    budget.limit=3;
    budget.allocationFailed();
    assert(budget.limit==2);
    islands[2].spilled=false;
    islands[2].distance=std::numeric_limits<float>::quiet_NaN();
    budget.select(islands,4,true,true);
    assert(!islands[2].wanted && islands[1].wanted);
    PcIslandCandidate edges[]={{0,20000,false,false,false,false},{0,120000,false,false,false,false}};
    budget.select(edges,2,true,true);
    assert(edges[0].wanted && !edges[1].wanted);
}
