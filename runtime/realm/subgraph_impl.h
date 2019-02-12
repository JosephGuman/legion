/* Copyright 2019 Stanford University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Realm subgraph implementation

#ifndef REALM_SUBGRAPH_IMPL_H
#define REALM_SUBGRAPH_IMPL_H

#include "realm/subgraph.h"
#include "realm/id.h"

namespace Realm {

  struct SubgraphScheduleEntry {
    SubgraphDefinition::OpKind op_kind;
    unsigned op_index;
    std::vector<int> preconditions;
  };

  class SubgraphImpl {
  public:
    SubgraphImpl();

    ~SubgraphImpl();

    void init(ID _me, int _owner);

    void instantiate(const void *args, size_t arglen,
		     const ProfilingRequestSet& prs,
		     const std::vector<Event>& preconditions,
		     std::vector<Event>& postconditions,
		     Event start_event, Event finish_event,
		     int priority_adjust);

  public:
    ID me;
    SubgraphImpl *next_free;
    SubgraphDefinition *defn;
    std::vector<SubgraphScheduleEntry> schedule;
  };


}; // namespace Realm

#endif
