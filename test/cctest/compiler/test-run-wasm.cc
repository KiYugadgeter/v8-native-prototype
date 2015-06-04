// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-graph.h"
#include "src/compiler/graph-visualizer.h"

#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-macro-gen.h"
#include "src/wasm/decoder.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/graph-builder-tester.h"
#include "test/cctest/compiler/value-helper.h"

#if V8_TURBOFAN_TARGET

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

#define LE32(x)                                                                \
  static_cast<byte>(x), static_cast<byte>(x >> 8), static_cast<byte>(x >> 16), \
      static_cast<byte>(x >> 24)


// Helpers for many common signatures that involve int32 types.
static AstType kIntTypes5[] = {kAstInt32, kAstInt32, kAstInt32, kAstInt32,
                               kAstInt32};

struct CommonSignatures {
  CommonSignatures()
      : sig_i_v(1, 0, kIntTypes5),
        sig_i_i(1, 1, kIntTypes5),
        sig_i_ii(1, 2, kIntTypes5),
        sig_i_iii(1, 3, kIntTypes5) {
    init_env(&env_i_v, &sig_i_v);
    init_env(&env_i_i, &sig_i_i);
    init_env(&env_i_ii, &sig_i_ii);
    init_env(&env_i_iii, &sig_i_iii);
  }

  FunctionSig sig_i_v;
  FunctionSig sig_i_i;
  FunctionSig sig_i_ii;
  FunctionSig sig_i_iii;
  FunctionEnv env_i_v;
  FunctionEnv env_i_i;
  FunctionEnv env_i_ii;
  FunctionEnv env_i_iii;

  void init_env(FunctionEnv* env, FunctionSig* sig) {
    env->module = nullptr;
    env->sig = sig;
    env->local_int32_count = 0;
    env->local_float64_count = 0;
    env->local_float32_count = 0;
    env->total_locals = sig->parameter_count();
  }
};


// A helper class to build graphs from Wasm bytecode, generate machine
// code, and run that code.
template <typename ReturnType>
class WasmRunner : public GraphBuilderTester<ReturnType> {
 public:
  WasmRunner(MachineType p0 = kMachNone, MachineType p1 = kMachNone,
             MachineType p2 = kMachNone, MachineType p3 = kMachNone,
             MachineType p4 = kMachNone)
      : GraphBuilderTester<ReturnType>(p0, p1, p2, p3, p4),
        jsgraph(this->isolate(), this->graph(), this->common(), nullptr,
                this->machine()) {
    if (p1 != kMachNone) {
      function_env = &sigs_.env_i_ii;
    } else if (p0 != kMachNone) {
      function_env = &sigs_.env_i_i;
    } else {
      function_env = &sigs_.env_i_v;
    }
  }

  JSGraph jsgraph;
  CommonSignatures sigs_;
  FunctionEnv* function_env;

  void Build(const byte* start, const byte* end) {
    Result result = BuildTFGraph(&jsgraph, function_env, start, end);
    if (result.error_msg != nullptr) {
      ptrdiff_t pc = result.error_pc - result.pc;
      ptrdiff_t pt = result.error_pt - result.pc;
      std::ostringstream str;
      str << "Verification failed: " << result.error_code << " pc = +" << pc
          << ", pt = +" << pt << ", msg = " << result.error_msg;
      FATAL(str.str().c_str());
    }
    if (FLAG_trace_turbo_graph) {
      OFStream os(stdout);
      os << AsRPO(*jsgraph.graph());
    }
  }

  byte AllocateLocal(AstType type) {
    int result = function_env->sig->parameter_count();
    if (type == kAstInt32) result += function_env->local_int32_count++;
    if (type == kAstFloat32) result += function_env->local_float32_count++;
    if (type == kAstFloat64) result += function_env->local_float64_count++;
    function_env->total_locals++;
    byte b = static_cast<byte>(result);
    CHECK_EQ(result, b);
    return b;
  }
};


#define BUILD(r, ...)                      \
  do {                                     \
    byte code[] = {__VA_ARGS__};           \
    r.Build(code, code + arraysize(code)); \
  } while (false)


TEST(Run_WasmInt8Const) {
  WasmRunner<int8_t> r;
  const byte kExpectedValue = 121;
  // return(kExpectedValue)
  BUILD(r, WASM_RETURN(1, WASM_INT8(kExpectedValue)));
  CHECK_EQ(kExpectedValue, r.Call());
}


TEST(Run_WasmInt8Const_all) {
  for (int value = -128; value <= 127; value++) {
    WasmRunner<int8_t> r;
    // return(value)
    BUILD(r, WASM_RETURN(1, WASM_INT8(value)));
    int8_t result = r.Call();
    CHECK_EQ(value, result);
  }
}


TEST(Run_WasmInt32Const) {
  WasmRunner<int32_t> r;
  const int32_t kExpectedValue = 0x11223344;
  // return(kExpectedValue)
  BUILD(r, WASM_RETURN(1, WASM_INT32(kExpectedValue)));
  CHECK_EQ(kExpectedValue, r.Call());
}


TEST(Run_WasmInt32Const_many) {
  FOR_INT32_INPUTS(i) {
    WasmRunner<int32_t> r;
    const int32_t kExpectedValue = *i;
    // return(kExpectedValue)
    BUILD(r, WASM_RETURN(1, WASM_INT32(kExpectedValue)));
    CHECK_EQ(kExpectedValue, r.Call());
  }
}


TEST(Run_WasmInt32Param0) {
  WasmRunner<int32_t> r(kMachInt32);
  // return(local[0])
  BUILD(r, WASM_RETURN(1, WASM_GET_LOCAL(0)));
  FOR_INT32_INPUTS(i) { CHECK_EQ(*i, r.Call(*i)); }
}


TEST(Run_WasmInt32Param1) {
  WasmRunner<int32_t> r(kMachInt32, kMachInt32);
  // return(local[1])
  BUILD(r, WASM_RETURN(1, WASM_GET_LOCAL(1)));
  FOR_INT32_INPUTS(i) { CHECK_EQ(*i, r.Call(-111, *i)); }
}


TEST(Run_WasmInt32Add) {
  WasmRunner<int32_t> r;
  // return 11 + 44
  BUILD(r, WASM_RETURN(1, WASM_INT32_ADD(WASM_INT8(11), WASM_INT8(44))));
  CHECK_EQ(55, r.Call());
}


TEST(Run_WasmInt32Add_P) {
  WasmRunner<int32_t> r(kMachInt32);
  // return p0 + 13
  BUILD(r, WASM_RETURN(1, WASM_INT32_ADD(WASM_INT8(13), WASM_GET_LOCAL(0))));
  FOR_INT32_INPUTS(i) { CHECK_EQ(*i + 13, r.Call(*i)); }
}


TEST(Run_WasmInt32Add_P2) {
  WasmRunner<int32_t> r(kMachInt32, kMachInt32);
  // return p0 + p1
  BUILD(r,
        WASM_RETURN(1, WASM_INT32_ADD(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1))));
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(static_cast<uint32_t>(*i) +
                                              static_cast<uint32_t>(*j));
      CHECK_EQ(expected, r.Call(*i, *j));
    }
  }
}

TEST(Run_WasmFloat32Add) {
  WasmRunner<int32_t> r;
  // return int(11.5f + 44.5f)
  BUILD(r, WASM_RETURN(1, WASM_INT32_FROM_FLOAT32(WASM_FLOAT32_ADD(
                              WASM_FLOAT32(11.5f), WASM_FLOAT32(44.5f)))));
  CHECK_EQ(56, r.Call());
}


TEST(Run_WasmFloat64Add) {
  WasmRunner<int32_t> r;
  // return int(13.5d + 43.5d)
  BUILD(r, WASM_RETURN(1, WASM_INT32_FROM_FLOAT64(WASM_FLOAT64_ADD(
                              WASM_FLOAT64(13.5), WASM_FLOAT64(43.5)))));
  CHECK_EQ(57, r.Call());
}


// TODO: test all Int32 binops

TEST(Run_Wasm_IfThen_P) {
  WasmRunner<int32_t> r(kMachInt32);
  // if (p0) return 11; else return 22;
  BUILD(r, WASM_IF_THEN(WASM_GET_LOCAL(0),                // --
                        WASM_RETURN(1, WASM_INT8(11)),    // --
                        WASM_RETURN(1, WASM_INT8(22))));  // --
  FOR_INT32_INPUTS(i) {
    int32_t expected = *i ? 11 : 22;
    CHECK_EQ(expected, r.Call(*i));
  }
}


TEST(Run_Wasm_Block_If_P) {
  WasmRunner<int32_t> r(kMachInt32);
  // { if (p0) return 51; return 52; }
  BUILD(r, WASM_BLOCK(2,                                       // --
                      WASM_IF(WASM_GET_LOCAL(0),               // --
                              WASM_RETURN(1, WASM_INT8(51))),  // --
                      WASM_RETURN(1, WASM_INT8(52))));         // --
  FOR_INT32_INPUTS(i) {
    int32_t expected = *i ? 51 : 52;
    CHECK_EQ(expected, r.Call(*i));
  }
}


TEST(Run_Wasm_Block_IfThen_P_assign) {
  WasmRunner<int32_t> r(kMachInt32);
  // { if (p0) p0 = 71; else p0 = 72; return p0; }
  BUILD(r, WASM_BLOCK(2,                                               // --
                      WASM_IF_THEN(WASM_GET_LOCAL(0),                  // --
                                   WASM_SET_LOCAL(0, WASM_INT8(71)),   // --
                                   WASM_SET_LOCAL(0, WASM_INT8(72))),  // --
                      WASM_RETURN(1, WASM_GET_LOCAL(0))));
  FOR_INT32_INPUTS(i) {
    int32_t expected = *i ? 71 : 72;
    CHECK_EQ(expected, r.Call(*i));
  }
}


TEST(Run_Wasm_Block_If_P_assign) {
  WasmRunner<int32_t> r(kMachInt32);
  // { if (p0) p0 = 61; return p0; }
  BUILD(r, WASM_BLOCK(
               2, WASM_IF(WASM_GET_LOCAL(0), WASM_SET_LOCAL(0, WASM_INT8(61))),
               WASM_RETURN(1, WASM_GET_LOCAL(0))));
  FOR_INT32_INPUTS(i) {
    int32_t expected = *i ? 61 : *i;
    CHECK_EQ(expected, r.Call(*i));
  }
}


TEST(Run_Wasm_Ternary_P) {
  WasmRunner<int32_t> r(kMachInt32);
  // return p0 ? 11 : 22;
  BUILD(r, WASM_RETURN(1, WASM_TERNARY(WASM_GET_LOCAL(0),  // --
                                       WASM_INT8(11),      // --
                                       WASM_INT8(22))));   // --
  FOR_INT32_INPUTS(i) {
    int32_t expected = *i ? 11 : 22;
    CHECK_EQ(expected, r.Call(*i));
  }
}


TEST(Run_Wasm_Comma_P) {
  WasmRunner<int32_t> r(kMachInt32);
  // return p0, 17;
  BUILD(r, WASM_RETURN(1, WASM_COMMA(WASM_GET_LOCAL(0), WASM_INT8(17))));
  FOR_INT32_INPUTS(i) { CHECK_EQ(17, r.Call(*i)); }
}


TEST(Run_Wasm_CountDown) {
  WasmRunner<int32_t> r(kMachInt32);
  BUILD(r,
        WASM_BLOCK(
            2, WASM_LOOP(2, WASM_IF(WASM_NOT(WASM_GET_LOCAL(0)), WASM_BREAK(0)),
                         WASM_SET_LOCAL(0, WASM_INT32_SUB(WASM_GET_LOCAL(0),
                                                          WASM_INT8(1)))),
            WASM_RETURN(1, WASM_GET_LOCAL(0))));
  CHECK_EQ(0, r.Call(1));
  CHECK_EQ(0, r.Call(10));
  CHECK_EQ(0, r.Call(100));
}


TEST(Run_Wasm_WhileCountDown) {
  WasmRunner<int32_t> r(kMachInt32);
  BUILD(r, WASM_BLOCK(
               2, WASM_WHILE(WASM_GET_LOCAL(0),
                             WASM_SET_LOCAL(0, WASM_INT32_SUB(WASM_GET_LOCAL(0),
                                                              WASM_INT8(1)))),
               WASM_RETURN(1, WASM_GET_LOCAL(0))));
  CHECK_EQ(0, r.Call(1));
  CHECK_EQ(0, r.Call(10));
  CHECK_EQ(0, r.Call(100));
}


TEST(Run_Wasm_LoadHeapInt32) {
  WasmRunner<int32_t> r(kMachInt32);
  ModuleEnv module;
  const int kSize = 5;
  int32_t buffer[kSize];
  module.heap_start = reinterpret_cast<uintptr_t>(&buffer);
  module.heap_end = reinterpret_cast<uintptr_t>(&buffer[kSize]);
  r.function_env->module = &module;

  BUILD(r, WASM_RETURN(1, WASM_GET_HEAP(kMemInt32, WASM_INT8(0))));

  buffer[0] = 999;
  CHECK_EQ(999, r.Call(0));

  buffer[0] = 888;
  CHECK_EQ(888, r.Call(0));

  buffer[0] = 777;
  CHECK_EQ(777, r.Call(0));
}


TEST(Run_Wasm_LoadHeapInt32_P) {
  WasmRunner<int32_t> r(kMachInt32);
  ModuleEnv module;
  const int kSize = 5;
  int32_t buffer[kSize] = {-99999999, -88888, -7777, 6666666, 565555};
  module.heap_start = reinterpret_cast<uintptr_t>(&buffer);
  module.heap_end = reinterpret_cast<uintptr_t>(&buffer[kSize]);
  r.function_env->module = &module;

  BUILD(r, WASM_RETURN(1, WASM_GET_HEAP(kMemInt32, WASM_GET_LOCAL(0))));

  for (int i = 0; i < kSize; i++) {
    CHECK_EQ(buffer[i], r.Call(i * 4));
  }
}


TEST(Run_Wasm_HeapInt32_Sum) {
  WasmRunner<int32_t> r(kMachInt32);
  const byte kSum = r.AllocateLocal(kAstInt32);
  ModuleEnv module;
  const int kSize = 5;
  int32_t buffer[kSize] = {-99999999, -88888, -7777, 6666666, 565555};
  module.heap_start = reinterpret_cast<uintptr_t>(&buffer);
  module.heap_end = reinterpret_cast<uintptr_t>(&buffer[kSize]);
  r.function_env->module = &module;

  BUILD(
      r,
      WASM_BLOCK(
          2, WASM_WHILE(
                 WASM_GET_LOCAL(0),
                 WASM_BLOCK(2, WASM_SET_LOCAL(
                                   kSum, WASM_INT32_ADD(
                                             WASM_GET_LOCAL(kSum),
                                             WASM_GET_HEAP(kMemInt32,
                                                           WASM_GET_LOCAL(0)))),
                            WASM_SET_LOCAL(0, WASM_INT32_SUB(WASM_GET_LOCAL(0),
                                                             WASM_INT8(4))))),
          WASM_RETURN(1, WASM_GET_LOCAL(1))));

  CHECK_EQ(7135556, r.Call(4 * (kSize - 1)));
}


TEST(Run_Wasm_HeapFloat32_Sum) {
  WasmRunner<int32_t> r(kMachInt32);
  const byte kSum = r.AllocateLocal(kAstFloat32);
  ModuleEnv module;
  const int kSize = 5;
  float buffer[kSize] = {-99.25, -888.25, -77.25, 66666.25, 5555.25};
  module.heap_start = reinterpret_cast<uintptr_t>(&buffer);
  module.heap_end = reinterpret_cast<uintptr_t>(&buffer[kSize]);
  r.function_env->module = &module;

  BUILD(
      r,
      WASM_BLOCK(
          3, WASM_WHILE(
                 WASM_GET_LOCAL(0),
                 WASM_BLOCK(2, WASM_SET_LOCAL(
                                   kSum, WASM_FLOAT32_ADD(
                                             WASM_GET_LOCAL(kSum),
                                             WASM_GET_HEAP(kMemFloat32,
                                                           WASM_GET_LOCAL(0)))),
                            WASM_SET_LOCAL(0, WASM_INT32_SUB(WASM_GET_LOCAL(0),
                                                             WASM_INT8(4))))),
          WASM_SET_HEAP(kMemFloat32, WASM_ZERO, WASM_GET_LOCAL(kSum)),
          WASM_RETURN(1, WASM_GET_LOCAL(0))));

  CHECK_EQ(0, r.Call(4 * (kSize - 1)));
  CHECK_NE(-99.25, buffer[0]);
  CHECK_EQ(71256.0f, buffer[0]);
}


template <typename T>
void GenerateAndRunFold(WasmOpcode binop, T* buffer, size_t size,
                        AstType astType, MemType memType) {
  WasmRunner<int32_t> r(kMachInt32);
  const byte kAccum = r.AllocateLocal(astType);
  ModuleEnv module;
  module.heap_start = reinterpret_cast<uintptr_t>(buffer);
  module.heap_end = reinterpret_cast<uintptr_t>(buffer + size);
  r.function_env->module = &module;

  BUILD(r,
        WASM_BLOCK(
            4, WASM_SET_LOCAL(kAccum, WASM_GET_HEAP(memType, WASM_ZERO)),
            WASM_WHILE(
                WASM_GET_LOCAL(0),
                WASM_BLOCK(
                    2, WASM_SET_LOCAL(
                           kAccum, WASM_BINOP(binop, WASM_GET_LOCAL(kAccum),
                                              WASM_GET_HEAP(
                                                  memType, WASM_GET_LOCAL(0)))),
                    WASM_SET_LOCAL(0, WASM_INT32_SUB(WASM_GET_LOCAL(0),
                                                     WASM_INT8(sizeof(T)))))),
            WASM_SET_HEAP(memType, WASM_ZERO, WASM_GET_LOCAL(kAccum)),
            WASM_RETURN(1, WASM_GET_LOCAL(0))));
  r.Call(static_cast<int>(sizeof(T) * (size - 1)));
}


TEST(Run_Wasm_HeapFloat64_Mul) {
  const size_t kSize = 6;
  double buffer[kSize] = {1, 2, 2, 2, 2, 2};
  GenerateAndRunFold<double>(kExprFloat64Mul, buffer, kSize, kAstFloat64,
                             kMemFloat64);
  CHECK_EQ(32, buffer[0]);
}
#endif  // V8_TURBOFAN_TARGET
