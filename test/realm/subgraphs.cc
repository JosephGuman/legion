#include "realm.h"
#include "realm/serialize.h"

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <csignal>
#include <cmath>

#include <time.h>

#include "osdep.h"

using namespace Realm;

Logger log_app("app");

// Task IDs, some IDs are reserved so start at first available number
enum {
  TOP_LEVEL_TASK = Processor::TASK_ID_FIRST_AVAILABLE+0,
  WRITER_TASK,
  READER_TASK,
  CLEANUP_TASK,
  DUMMY_TASK,
};

enum {
  FID_DATA = 100,
};

enum {
  REDOP_INT_ADD = 60,
};

class ReductionOpIntAdd {
public:
  typedef int LHS;
  typedef int RHS;

  template <bool EXCL>
  static void apply(LHS& lhs, RHS rhs) { lhs += rhs; }

  // both of these are optional
  static const RHS identity;

  template <bool EXCL>
  static void fold(RHS& rhs1, RHS rhs2) { rhs1 += rhs2; }
};

const ReductionOpIntAdd::RHS ReductionOpIntAdd::identity = 0;

struct WriterTaskArgs {
  IndexSpace<1> is;
  RegionInstance inst;
  int wrval;
};

struct ReaderTaskArgs {
  IndexSpace<1> is;
  RegionInstance inst;
  int rdval;
};

void writer_task(const void *args, size_t arglen,
                 const void *userdata, size_t userlen, Processor p)
{
  const WriterTaskArgs& wargs = *reinterpret_cast<const WriterTaskArgs *>(args);
  AffineAccessor<int, 1> acc(wargs.inst, FID_DATA);
  for(IndexSpaceIterator<1> it(wargs.is); it.valid; it.step())
    for(PointInRectIterator<1> it2(it.rect); it2.valid; it2.step()) {
      acc[it2.p] = it2.p[0] + wargs.wrval;
    }
}

int correct = 0;

struct TaskArgs {
  int32_t value;
};

void dummy_task(const void *args, size_t arglen,
                const void *userdata, size_t userlen, Processor p) {
  if (arglen > 0) {
    int32_t value = *(int32_t*)(args);
    std::cout << "In dummy task -- " << value << "!" << std::endl;
  } else {
    std::cout << "In dummy task!" << std::endl;
  }
}

void reader_task(const void *args, size_t arglen,
                 const void *userdata, size_t userlen, Processor p)
{
  const ReaderTaskArgs& rargs = *reinterpret_cast<const ReaderTaskArgs *>(args);
  AffineAccessor<int, 1> acc(rargs.inst, FID_DATA);
  for(IndexSpaceIterator<1> it(rargs.is); it.valid; it.step())
    for(PointInRectIterator<1> it2(it.rect); it2.valid; it2.step()) {
      int expval = it2.p[0] + rargs.rdval;
      int actval = acc[it2.p];
      if(expval == actval)
        correct++;
      else
        log_app.error() << "MISMATCH: " << it2.p << ": " << actval << " != " << expval;
    }
}

struct CleanupTaskArgs {
  Event precond;
  Subgraph subgraph, subgraph_inner;
  UserEvent cleanup_done;
};

struct InnerSubgraphArgs {
  Barrier b;
  int arr_value;
};

struct SubgraphArgs {
  int rdwr_val;
  int arr_inc;
};

void cleanup_task(const void *args, size_t arglen,
                  const void *userdata, size_t userlen, Processor p)
{
  const CleanupTaskArgs& cta = *static_cast<const CleanupTaskArgs *>(args);

  SubgraphArgs sg_args;
  sg_args.rdwr_val = 200;
  sg_args.arr_inc = 10;

  // no external preconditions on this one since the whole instantiation
  //  uses a global precondition
  std::vector<Event> preconditions;
  std::vector<Event> postconditions(1);
  Event e = cta.subgraph.instantiate(&sg_args, sizeof(sg_args),
                                     ProfilingRequestSet(),
                                     preconditions,
                                     postconditions,
                                     cta.precond);

  // technically e already dominates postconditions[0], but do this to make
  //  sure the external postcondition actually triggers
  e = Event::merge_events(e, postconditions[0]);

  // destroy the subgraph and trigger the done event upon completion of this
  //  instance
  cta.subgraph.destroy(e);
  cta.subgraph_inner.destroy(e);
  cta.cleanup_done.trigger(e);
}

// clang doesn't permit use of offsetof when there are non-POD types involved
// and gcc doesn't permit computing the address of a dereference of a nullptr
template <typename T, typename U>
static size_t compute_offset(U T::*field_ptr)
{
  T thing;
  size_t ofs = (reinterpret_cast<size_t>(&(thing.*field_ptr)) -
                reinterpret_cast<size_t>(&thing));
  return ofs;
}

#define OFFSETOF(type, field) \
  compute_offset<type>(&type::field)

void top_level_task(const void *args, size_t arglen,
                    const void *userdata, size_t userlen, Processor p)
{
  log_app.print() << "Realm subgraphs test";
  auto pq = Machine::ProcessorQuery(Machine::get_machine()).only_kind(Processor::LOC_PROC);
  std::vector<Processor> cpus(pq.begin(), pq.end());

//  auto pq = Machine::ProcessorQuery(Machine::get_machine()).only_kind(Processor::TOC_PROC);
//  std::vector<Processor> gpus(pq.begin(), pq.end());

//  auto mq = Machine::MemoryQuery(Machine::get_machine()).only_kind(Memory::GPU_FB_MEM);
//  std::vector<Memory> fbmems(mq.begin(), mq.end());

//  auto sq = Machine::MemoryQuery(Machine::get_machine()).only_kind(Memory::SYSTEM_MEM);
//  std::vector<Memory> sysmems(sq.begin(), sq.end());

  // Create a buffer.
//  IndexSpace<1> is = Rect<1>(0, 0);
//  std::map<FieldID, size_t> field_sizes;
//  field_sizes[0] = sizeof(int32_t);
//  RegionInstance gpu_inst;
//  RegionInstance::create_instance(gpu_inst, fbmems[0],
//                                  is, field_sizes,
//                                  0 /*SOA*/, ProfilingRequestSet()).wait();

  // IndexSpace<1> is2 = Rect<1>(0, 1250000000);
//  IndexSpace<1> is2 = Rect<1>(0, 10);
//  RegionInstance src_inst, dst_inst, cpu_inst, fill_inst;
//  RegionInstance::create_instance(src_inst, fbmems[0],
//                                  is2, field_sizes,
//                                  0 /*SOA*/, ProfilingRequestSet()).wait();
//  RegionInstance::create_instance(dst_inst, fbmems[0],
//                                  is2, field_sizes,
//                                  0 /*SOA*/, ProfilingRequestSet()).wait();
//  RegionInstance::create_instance(fill_inst, fbmems[0],
//                                  is2, field_sizes,
//                                  0 /*SOA*/, ProfilingRequestSet()).wait();
//  RegionInstance::create_instance(cpu_inst, sysmems[0],
//                                  is2, field_sizes,
//                                  0 /*SOA*/, ProfilingRequestSet()).wait();

  // Fill the source.
//  {
//    std::vector<CopySrcDstField> info(1);
//    info[0].set_field(src_inst, 0, sizeof(int32_t));
//    int32_t value = 42;
//    is2.fill(info, ProfilingRequestSet(), &value, sizeof(int32_t)).wait();
//  }
  // Also fill the destination to be sure the copy did something.
//  {
//    std::vector<CopySrcDstField> info(1);
//    info[0].set_field(dst_inst, 0, sizeof(int32_t));
//    int32_t value = 15210;
//    is2.fill(info, ProfilingRequestSet(), &value, sizeof(int32_t)).wait();
//  }
  // Fill another instance with a bogus value to be filled by the
  // subgraph.
//  {
//    std::vector<CopySrcDstField> info(1);
//    info[0].set_field(fill_inst, 0, sizeof(int32_t));
//    int32_t value = 15210;
//    is2.fill(info, ProfilingRequestSet(), &value, sizeof(int32_t)).wait();
//  }

  auto bar = Barrier::create_barrier(1);

  std::cout << "Diamond subgraph." << std::endl;
  Subgraph diamond;
  {
    SubgraphDefinition sd;
    sd.concurrency_mode = SubgraphDefinition::INSTANTIATION_ORDER;
    // Add four diamond tasks.
    sd.tasks.resize(5);
    for (int i = 0; i < 5; i++) {
      // sd.tasks[i].proc = gpus[(i % 2) % gpus.size()];
      sd.tasks[i].task_id = DUMMY_TASK;
      // sd.tasks[i].proc = p;
    }

    sd.tasks[0].proc = cpus[0];
    sd.tasks[1].proc = cpus[1];
    sd.tasks[2].proc = cpus[2];
    // sd.tasks[3].proc = gpus[1];
    sd.tasks[3].proc = cpus[3];
    sd.tasks[4].proc = cpus[2];

    TaskArgs args;
    args.value = 0;
    sd.tasks[0].args = ByteArray(&args, sizeof(TaskArgs));
    sd.tasks[1].args = ByteArray(&args, sizeof(TaskArgs));
    sd.tasks[2].args = ByteArray(&args, sizeof(TaskArgs));
    sd.tasks[3].args = ByteArray(&args, sizeof(TaskArgs));
    sd.tasks[4].args = ByteArray(&args, sizeof(TaskArgs));

    sd.interpolations.resize(6);
    sd.interpolations[0].offset = 0;
    sd.interpolations[0].bytes = sizeof(int32_t);
    sd.interpolations[0].target_kind = SubgraphDefinition::Interpolation::TARGET_TASK_ARGS;
    sd.interpolations[0].target_index = 0;
    sd.interpolations[0].target_offset = 0;

    sd.interpolations[1].offset = 4;
    sd.interpolations[1].bytes = sizeof(int32_t);
    sd.interpolations[1].target_kind = SubgraphDefinition::Interpolation::TARGET_TASK_ARGS;
    sd.interpolations[1].target_index = 1;
    sd.interpolations[1].target_offset = 0;

    sd.interpolations[2].offset = 8;
    sd.interpolations[2].bytes = sizeof(int32_t);
    sd.interpolations[2].target_kind = SubgraphDefinition::Interpolation::TARGET_TASK_ARGS;
    sd.interpolations[2].target_index = 2;
    sd.interpolations[2].target_offset = 0;

    sd.interpolations[3].offset = 12;
    sd.interpolations[3].bytes = sizeof(int32_t);
    sd.interpolations[3].target_kind = SubgraphDefinition::Interpolation::TARGET_TASK_ARGS;
    sd.interpolations[3].target_index = 3;
    sd.interpolations[3].target_offset = 0;

    sd.interpolations[4].offset = 16;
    sd.interpolations[4].bytes = sizeof(int32_t);
    sd.interpolations[4].target_kind = SubgraphDefinition::Interpolation::TARGET_TASK_ARGS;
    sd.interpolations[4].target_index = 4;
    sd.interpolations[4].target_offset = 0;

    sd.arrivals.resize(1);
    sd.arrivals[0].barrier = bar;
    sd.interpolations[5].offset = 20;
    sd.interpolations[5].bytes = sizeof(Barrier);
    sd.interpolations[5].target_kind = SubgraphDefinition::Interpolation::TARGET_ARRIVAL_BARRIER;


//    sd.copies.resize(2);
//    {
//      std::vector<CopySrcDstField> src(1), dst(1);
//      ProfilingRequestSet prs();
//      src[0].set_field(src_inst, 0, sizeof(int32_t));
//      dst[0].set_field(dst_inst, 0, sizeof(int32_t));
//      sd.copies[0].space = is2;
//      sd.copies[0].srcs = src;
//      sd.copies[0].dsts = dst;
//    }
//    {
//      std::vector<CopySrcDstField> src(1), dst(1);
//      ProfilingRequestSet prs();
//      int32_t value = 13;
//      src[0].set_fill(value);
//      dst[0].set_field(fill_inst, 0, sizeof(int32_t));
//      sd.copies[1].space = is2;
//      sd.copies[1].srcs = src;
//      sd.copies[1].dsts = dst;
//    }


    // Add the necessary dependencies.
    // sd.dependencies.resize(4);
    sd.dependencies.resize(7);
    // TODO (rohany): Not considering external pre/postconditions etc.
    // Add the incoming and outgoing dependencies.
    sd.dependencies[0].src_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[0].src_op_index = 0;
    sd.dependencies[0].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[0].tgt_op_index = 1;

    sd.dependencies[1].src_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[1].src_op_index = 0;
    sd.dependencies[1].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[1].tgt_op_index = 2;

    sd.dependencies[2].src_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[2].src_op_index = 1;
    sd.dependencies[2].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[2].tgt_op_index = 3;

    sd.dependencies[3].src_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[3].src_op_index = 2;
    sd.dependencies[3].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[3].tgt_op_index = 3;


    // Extra ...
    sd.dependencies[4].src_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[4].src_op_index = 0;
    sd.dependencies[4].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[4].tgt_op_index = 3;

    sd.dependencies[5].src_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[5].src_op_index = 1;
    sd.dependencies[5].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[5].tgt_op_index = 2;

    sd.dependencies[6].src_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[6].src_op_index = 1;
    sd.dependencies[6].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[6].tgt_op_index = 4;

    // External precondition ...
    sd.dependencies[7].src_op_kind = SubgraphDefinition::OPKIND_EXT_PRECOND;
    // TODO (rohany): Try making this not the first etc ...
    sd.dependencies[7].src_op_index = 0;
    sd.dependencies[7].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
    sd.dependencies[7].tgt_op_index = 0;

//    // Copy dependencies.
//    sd.dependencies[4].src_op_kind = SubgraphDefinition::OPKIND_TASK;
//    sd.dependencies[4].src_op_index = 0;
//    sd.dependencies[4].tgt_op_kind = SubgraphDefinition::OPKIND_COPY;
//    sd.dependencies[4].tgt_op_index = 0;
//
//    sd.dependencies[5].src_op_kind = SubgraphDefinition::OPKIND_TASK;
//    sd.dependencies[5].src_op_index = 1;
//    sd.dependencies[5].tgt_op_kind = SubgraphDefinition::OPKIND_COPY;
//    sd.dependencies[5].tgt_op_index = 1;


    // TODO (rohany): Let's just first add a copy and see if nothing explodes.

    Subgraph::create_subgraph(diamond, sd, ProfilingRequestSet()).wait();
  }

  auto next_bar = bar.advance_barrier();
  // TODO (rohany): Eventually chain these rather than have waits in the middle.
  Event e = Event::NO_EVENT;
  Serialization::DynamicBufferSerializer ser(256);
  ser << 0;
  ser << 1;
  ser << 2;
  ser << 3;
  ser << 4;
  ser << next_bar;
  // e = diamond.instantiate(ser.get_buffer(), ser.bytes_used(), ProfilingRequestSet(), e);
  std::vector<Event> ext_preconds;
  ext_preconds.push_back(bar);
  std::vector<Event> ext_postconds;
  e = diamond.instantiate(ser.get_buffer(), ser.bytes_used(), ProfilingRequestSet(), ext_preconds, ext_postconds);
//  std::cout << "Starting second instantiation." << std::endl;
  ser.reset();

  std::cout << "Triggering barrier arrival." << std::endl;
  bar.arrive();

  // e.wait();

  // bar = bar.advance_barrier();

  // See if the subgraph did the copy.
//  {
//    std::vector<CopySrcDstField> src(1), dst(1);
//    src[0].set_field(dst_inst, 0, sizeof(int32_t));
//    dst[0].set_field(cpu_inst, 0, sizeof(int32_t));
//    is2.copy(src, dst, ProfilingRequestSet()).wait();
//
//    AffineAccessor<int32_t, 1> acc = AffineAccessor<int32_t, 1>(cpu_inst, 0);
//    std::cout << "FOUND VALUE IN INST: " << (*acc.ptr(0)) << std::endl;
//    std::cout << "FOUND VALUE IN INST: " << (*acc.ptr(is2.bounds.hi[0])) << std::endl;
//  }


  // Check out the fill instance.
//  {
//    std::vector<CopySrcDstField> src(1), dst(1);
//    src[0].set_field(fill_inst, 0, sizeof(int32_t));
//    dst[0].set_field(cpu_inst, 0, sizeof(int32_t));
//    is2.copy(src, dst, ProfilingRequestSet()).wait();
//    AffineAccessor<int32_t, 1> acc = AffineAccessor<int32_t, 1>(cpu_inst, 0);
//    std::cout << "FOUND VALUE IN INST: " << (*acc.ptr(0)) << std::endl;
//    std::cout << "FOUND VALUE IN INST: " << (*acc.ptr(is2.bounds.hi[0])) << std::endl;
//  }
//
//  interp = 2;


  auto next_next_bar = next_bar.advance_barrier();
  // Set interp to some new data, just to be sure it works on a second replay.
  ser << 5;
  ser << 6;
  ser << 7;
  ser << 8;
  ser << 9;
  ser << next_next_bar;

  ext_preconds.clear();
  ext_preconds.push_back(next_bar);
  e = diamond.instantiate(ser.get_buffer(), ser.bytes_used(), ProfilingRequestSet(), ext_preconds, ext_postconds, e);
  std::cout << "Starting third instantiation." << std::endl;
//  interp = 3;
//  e = diamond.instantiate(&interp, sizeof(interp), ProfilingRequestSet(), e);
//  std::cout << "Starting fourth instantiation." << std::endl;
//  interp = 4;
//  e = diamond.instantiate(&interp, sizeof(interp), ProfilingRequestSet(), e);
  // e.wait();

  next_next_bar.wait();

  e.wait();
  std::cout << "Done!" << std::endl;

  // do everything on this processor - get a good memory to use
//  Memory m = Machine::MemoryQuery(Machine::get_machine()).has_affinity_to(p).first();
//  assert(m.exists());
//
//  IndexSpace<1> is = Rect<1>(0, 9);
//  RegionInstance inst1, inst2;
//  std::map<FieldID, size_t> field_sizes;
//  field_sizes[FID_DATA] = sizeof(int);
//
//  Event e = RegionInstance::create_instance(inst1, m, is, field_sizes,
//					    0 /*block_size=SOA*/,
//					    ProfilingRequestSet());
//
//  e = RegionInstance::create_instance(inst2, m, is, field_sizes,
//				      0 /*block_size=SOA*/,
//				      ProfilingRequestSet(), e);
//
//  // immediate mode - no subgraph
//  WriterTaskArgs w_args;
//  w_args.is = is;
//  w_args.inst = inst1;
//  w_args.wrval = 5;
//  e = p.spawn(WRITER_TASK, &w_args, sizeof(w_args), e);
//
//  std::vector<CopySrcDstField> copy_src(1), copy_dst(1);
//  copy_src[0].set_field(inst1, FID_DATA, sizeof(int));
//  copy_dst[0].set_field(inst2, FID_DATA, sizeof(int));
//  e = is.copy(copy_src, copy_dst, ProfilingRequestSet(), e);
//
//  ReaderTaskArgs r_args;
//  r_args.is = is;
//  r_args.inst = inst2;
//  r_args.rdval = 5;
//  e = p.spawn(READER_TASK, &r_args, sizeof(r_args), e);
//
//  e.wait();
//
//  // create an inner subgraph that just does a barrier arrival
//  Subgraph sg_inner;
//  {
//    SubgraphDefinition sd_inner;
//
//    sd_inner.arrivals.resize(1);
//    sd_inner.arrivals[0].barrier = Barrier::NO_BARRIER; // will be interpolated
//    sd_inner.arrivals[0].count = 1;
//    int rv = 5;
//    sd_inner.arrivals[0].reduce_value.set(&rv, sizeof(rv));
//
//    sd_inner.interpolations.resize(2);
//    sd_inner.interpolations[0].offset = OFFSETOF(InnerSubgraphArgs, b);
//    sd_inner.interpolations[0].bytes = sizeof(Barrier);
//    sd_inner.interpolations[0].target_kind = SubgraphDefinition::Interpolation::TARGET_ARRIVAL_BARRIER;
//    sd_inner.interpolations[0].target_index = 0;
//    sd_inner.interpolations[0].target_offset = 0;
//    sd_inner.interpolations[0].redop_id = 0;
//
//    sd_inner.interpolations[1].offset = OFFSETOF(InnerSubgraphArgs, arr_value);
//    sd_inner.interpolations[1].bytes = sizeof(int);
//    sd_inner.interpolations[1].target_kind = SubgraphDefinition::Interpolation::TARGET_ARRIVAL_VALUE;
//    sd_inner.interpolations[1].target_index = 0;
//    sd_inner.interpolations[1].target_offset = 0;
//    sd_inner.interpolations[1].redop_id = 0;
//
//    Subgraph::create_subgraph(sg_inner,
//			      sd_inner,
//			      ProfilingRequestSet()).wait();
//  }
//
//  Reservation rsrv = Reservation::create_reservation();
//
//  int ival = 77;
//  Barrier barrier = Barrier::create_barrier(5, REDOP_INT_ADD,
//					    &ival, sizeof(ival));
//
//  SubgraphDefinition sd;
//  sd.tasks.resize(2);
//  sd.tasks[0].proc = p;
//  sd.tasks[0].task_id = WRITER_TASK;
//  w_args.wrval = 6;
//  sd.tasks[0].args.set(&w_args, sizeof(w_args));
//  sd.tasks[1].proc = p;
//  sd.tasks[1].task_id = READER_TASK;
//  r_args.rdval = 7;
//  sd.tasks[1].args.set(&r_args, sizeof(r_args));
//
//  sd.copies.resize(1);
//  sd.copies[0].space = is;
//  sd.copies[0].srcs = copy_src;
//  sd.copies[0].dsts = copy_dst;
//
//  sd.instantiations.resize(1);
//  sd.instantiations[0].subgraph = sg_inner;
//  InnerSubgraphArgs inner_args;
//  inner_args.b = barrier;
//  inner_args.arr_value = 7;
//  sd.instantiations[0].args.set(&inner_args, sizeof(inner_args));
//  sd.instantiations[0].priority_adjust = 0;
//
//  sd.acquires.resize(1);
//  sd.acquires[0].rsrv = rsrv;
//
//  sd.releases.resize(1);
//  sd.releases[0].rsrv = rsrv;
//
//  sd.dependencies.resize(7);
//  sd.dependencies[0].src_op_kind = SubgraphDefinition::OPKIND_TASK;
//  sd.dependencies[0].src_op_index = 0;
//  sd.dependencies[0].tgt_op_kind = SubgraphDefinition::OPKIND_COPY;
//  sd.dependencies[0].tgt_op_index = 0;
//
//  sd.dependencies[1].src_op_kind = SubgraphDefinition::OPKIND_COPY;
//  sd.dependencies[1].src_op_index = 0;
//  sd.dependencies[1].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
//  sd.dependencies[1].tgt_op_index = 1;
//
//  sd.dependencies[2].src_op_kind = SubgraphDefinition::OPKIND_ACQUIRE;
//  sd.dependencies[2].src_op_index = 0;
//  sd.dependencies[2].tgt_op_kind = SubgraphDefinition::OPKIND_TASK;
//  sd.dependencies[2].tgt_op_index = 0;
//
//  sd.dependencies[3].src_op_kind = SubgraphDefinition::OPKIND_TASK;
//  sd.dependencies[3].src_op_index = 1;
//  sd.dependencies[3].tgt_op_kind = SubgraphDefinition::OPKIND_RELEASE;
//  sd.dependencies[3].tgt_op_index = 0;
//
//  sd.dependencies[4].src_op_kind = SubgraphDefinition::OPKIND_EXT_PRECOND;
//  sd.dependencies[4].src_op_index = 0;
//  sd.dependencies[4].tgt_op_kind = SubgraphDefinition::OPKIND_ACQUIRE;
//  sd.dependencies[4].tgt_op_index = 0;
//
//  sd.dependencies[5].src_op_kind = SubgraphDefinition::OPKIND_ACQUIRE;
//  sd.dependencies[5].src_op_index = 0;
//  sd.dependencies[5].tgt_op_kind = SubgraphDefinition::OPKIND_EXT_POSTCOND;
//  sd.dependencies[5].tgt_op_index = 0;
//
//  sd.dependencies[6].src_op_kind = SubgraphDefinition::OPKIND_ACQUIRE;
//  sd.dependencies[6].src_op_index = 0;
//  sd.dependencies[6].tgt_op_kind = SubgraphDefinition::OPKIND_INSTANTIATION;
//  sd.dependencies[6].tgt_op_index = 0;
//  sd.dependencies[6].tgt_op_port = 1;
//
//  sd.interpolations.resize(3);
//  sd.interpolations[0].offset = OFFSETOF(SubgraphArgs, rdwr_val);
//  sd.interpolations[0].bytes = sizeof(int);
//  sd.interpolations[0].target_kind = SubgraphDefinition::Interpolation::TARGET_TASK_ARGS;
//  sd.interpolations[0].target_index = 0;
//  sd.interpolations[0].target_offset = OFFSETOF(WriterTaskArgs, wrval);
//
//  sd.interpolations[1].offset = OFFSETOF(SubgraphArgs, rdwr_val);
//  sd.interpolations[1].bytes = sizeof(int);
//  sd.interpolations[1].target_kind = SubgraphDefinition::Interpolation::TARGET_TASK_ARGS;
//  sd.interpolations[1].target_index = 1;
//  sd.interpolations[1].target_offset = OFFSETOF(ReaderTaskArgs, rdval);
//
//  sd.interpolations[2].offset = OFFSETOF(SubgraphArgs, arr_inc);
//  sd.interpolations[2].bytes = sizeof(int);
//  sd.interpolations[2].target_kind = SubgraphDefinition::Interpolation::TARGET_INSTANCE_ARGS;
//  sd.interpolations[2].target_index = 0;
//  sd.interpolations[2].target_offset = OFFSETOF(InnerSubgraphArgs, arr_value);
//  sd.interpolations[2].redop_id = REDOP_INT_ADD;
//
//  Subgraph sg;
//  e = Subgraph::create_subgraph(sg,
//				sd,
//				ProfilingRequestSet());
//
//  std::vector<UserEvent> start_events(4);
//  std::vector<Event> acquire_events(4);
//  std::vector<Event> finish_events(4);
//  for(int i = 0; i < 4; i++) {
//    SubgraphArgs sg_args;
//    sg_args.rdwr_val = 100 * (i + 1);
//    sg_args.arr_inc = (i + 1);
//    start_events[i] = UserEvent::create_user_event();
//    std::vector<Event> preconds(1, start_events[i]);
//    std::vector<Event> postconds(1);
//    finish_events[i] = sg.instantiate(&sg_args, sizeof(sg_args),
//				      ProfilingRequestSet(),
//				      preconds,
//				      postconds,
//				      e);
//    acquire_events[i] = postconds[0];
//  }
//  e = Event::merge_events(finish_events);
//
//  // do one more invocation on a remote processor if we can, and have it
//  //  do the subgraph cleanup
//  Machine::ProcessorQuery pq(Machine::get_machine());
//  pq.only_kind(Processor::LOC_PROC);
//  Processor lastp = Processor::NO_PROC;
//  for(Machine::ProcessorQuery::iterator it = pq.begin(); it != pq.end(); ++it)
//    lastp = *it;
//  assert(lastp.exists());
//
//  // pass the merged finish event to this task as an arg, not a precondition
//  //  of the task itself
//  CleanupTaskArgs cleanup_args;
//  cleanup_args.precond = e;
//  cleanup_args.subgraph = sg;
//  cleanup_args.subgraph_inner = sg_inner;
//  cleanup_args.cleanup_done = UserEvent::create_user_event();
//  lastp.spawn(CLEANUP_TASK, &cleanup_args, sizeof(CleanupTaskArgs),
//	      ProfilingRequestSet());
//
//  // connect up start events
//  start_events[0].trigger(acquire_events[1]);
//  start_events[1].trigger(acquire_events[2]);
//  start_events[2].trigger(acquire_events[3]);
//  start_events[3].trigger();
//
//  cleanup_args.cleanup_done.wait();
//
//  bool ok = true;
//
//  // make sure the barrier got triggered with the right values
//  barrier.wait();
//  int exp_bar_value = 77 + (7 + 1) + (7 + 2) + (7 + 3) + (7 + 4) + (7 + 10);
//  int act_bar_value = 0;
//  {
//    bool ok = barrier.get_result(&act_bar_value, sizeof(act_bar_value));
//    assert(ok);
//  }
//  if(exp_bar_value != act_bar_value) {
//    log_app.error() << "barrier value mismatch: exp=" << exp_bar_value
//		    << " act=" << act_bar_value;
//    ok = false;
//  }
//
//  int expcorrect = 60;  // counting the cleanup task
//  if(expcorrect != correct) {
//    log_app.error() << correct << " correct comparisons (out of " << expcorrect << ")";
//    ok = false;
//  }

  bool ok = true;
  Runtime::get_runtime().shutdown(Event::NO_EVENT,
                                  ok ? 0 : 1);
}

int main(int argc, char **argv)
{
  Runtime rt;

  rt.init(&argc, &argv);

#if 0
  for(int i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "-b")) {
      buffer_size = strtoll(argv[++i], 0, 10);
      continue;
    }

  }
#endif

  rt.register_task(TOP_LEVEL_TASK, top_level_task);
  rt.register_task(WRITER_TASK, writer_task);
  rt.register_task(READER_TASK, reader_task);
  rt.register_task(CLEANUP_TASK, cleanup_task);
  rt.register_task(DUMMY_TASK, dummy_task);
  // Processor::register_task_by_kind(Processor::TOC_PROC, false, DUMMY_TASK, CodeDescriptor(dummy_task), ProfilingRequestSet());

  rt.register_reduction<ReductionOpIntAdd>(REDOP_INT_ADD);

  // select a processor to run the top level task on
  Processor p = Machine::ProcessorQuery(Machine::get_machine())
      .only_kind(Processor::LOC_PROC)
      .first();
  assert(p.exists());

  // collective launch of a single main task
  rt.collective_spawn(p, TOP_LEVEL_TASK, 0, 0);

  // main task will call shutdown - wait for that and return the exit code
  return rt.wait_for_shutdown();
}
