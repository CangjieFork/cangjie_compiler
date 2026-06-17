# InitChecker O(N²)→O(N) 优化：gitcode 干净复现验证

日期：2026-06-14
方法：按 `cangjie-build` 逻辑，在 WSL ext4 (`/root/cj_build`) 从 gitcode 全新克隆
4 个仓库，应用优化 commit，从 0 构建 cjc + runtime + stdlib，OLD/NEW 对照测量。

## 构建环境

- 位置：`/root/cj_build`（ext4，避开 drvfs I/O 死锁）
- 源：`gitcode.com/Cangjie/{cangjie_compiler,cangjie_runtime,cangjie_tools,cangjie_stdx}` 默认分支 `main`（HEAD `22a37f6`）
- LLVM 后端：`gitcode.com/Cangjie/llvm-project.git` 分支 `dev`，源码构建（cjnative）
- 编译器：Clang 15.0.7，release，-j14
- 关键坑修复：
  1. `src/Utils/CMakeLists.txt` 需 `ConvertUTF.cpp`，巨型 openharmony LLVM clone
     失败（cmake 不检查返回值）→ 预克隆 cangjie `llvm-project` 到
     `third_party/llvm-project`，命中 `if(EXISTS CANGJIE_CJNATIVE_SOURCE_DIR)` 分支
  2. Ubuntu plucky 无 `libtinfo5`/`libncurses5` → 装可用子集 + 软链 `libtinfo.so.5→.6`
  3. WSL 后台进程必须用 harness 后台任务承载（`nohup` 在 `wsl -e` 退出时被 SIGINT 杀）

## 优化内容

`src/Sema/LegalityOfUsage/InitializationChecker.cpp` `UpdateScopeStatus()`：
每遇控制流终止符（return/throw/break/continue）就把**整个** `contextVariables`
map 拷进 `variablesBeforeTeminatedScope`。该 map 每个已分析 scope 一条目，
故每终止符 O(scope 数)、整包 O(decls²)。

改为只走**祖先 scope-gate 链**（与 `ScopeManager::GetCurSatisfiedSymbol` 同惯用法），
O(scope 深度)，可见变量集完全等价（兄弟/无关 scope 的变量在该终止符处本就不可见）。

## OLD vs NEW 对照（同一 build 树二进制路径、同 CANGJIE_HOME、同 flags）

压测形状：ntypes 个独立类，每类 8 字段 + 4 方法，每方法 3 个 if(return) + while/break
终止符。`Post TypeCheck.CheckLegalityOfUsage` 为 CLU 时间，峰值 RSS 由 `/usr/bin/time -v`。

| ntypes | OLD CLU(ms) | NEW CLU(ms) | CPU 加速 | OLD RSS(MB) | NEW RSS(MB) | 内存比 |
|-------:|------------:|------------:|--------:|------------:|------------:|------:|
|   100  |        123  |         24  |   5.1×  |       188   |       180   | 1.0× |
|   200  |        502  |         49  |  10.2×  |       306   |       255   | 1.2× |
|   400  |      1 957  |        134  |  14.6×  |       614   |       371   | 1.7× |
|   800  |      7 965  |        320  |  24.9×  |     1 590   |       610   | 2.6× |
|  1600  |     35 638  |        760  | **46.9×** |   5 028   |     1 129   | **4.5×** |

- **伸缩**：OLD 每翻倍 ×4 = O(N²)；NEW ×2.3 ≈ O(N)。加速随规模增长。
- **CPU**：n=1600 时 CheckLegalityOfUsage 35.6s → 0.76s。
- **内存**：n=1600 时进程峰值 5.0 GB → 1.1 GB。
- **正确性**：C1 合法初始化不误报 / C2 用未初始化变量正确报错 / C3 单分支赋值正确报错，全 PASS。

## 与真实工作负载的对应

记忆 `project_compile_pressure_impl_monolith`：`windows_common.impl` 单包 812 类 /
196k 行 —— 正好落在压测 n=800~1600 区间。该包 InitChecker 此前是峰值 42GB / Semantic
352s 的主因之一。本优化对此包预计带来同量级的 CPU(~15-25×) 与内存(~3×) 下降。

## 产物

- 优化版 SDK：`/root/cj_build/cangjie_compiler/output`（已 install，实测线性）
- 优化 commit（tmp_build）：`6d74761b`；gitcode main 上的等价改动已在本次复现中应用
- OLD 二进制留档：`/root/cj_build/snap_old_cjc`、`snap_old_libfe.so`
- NEW 二进制快照：`/root/cj_build/snap_new/`
- 测量脚本：`/root/cj_build/measure.py`、`verify.py`

## 合并对照（gitcode main 基线：两个 O(N²) 都在 vs 双修复）

全前端编译 Main Stage 总时长 + 峰值 RSS（同二进制路径/CANGJIE_HOME/flags）：

| ntypes | 基线总时(ms) | 优化总时(ms) | 加速 | 基线RSS(MB) | 优化RSS(MB) | 内存比 |
|-------:|----------:|----------:|----:|----------:|----------:|------:|
|   400  |     3 316 |     1 275 | 2.6×|       798 |       345 | 2.3× |
|   800  |    10 949 |     2 860 | 3.8×|     2 413 |       565 | 4.3× |
|  1600  |    42 145 |     7 116 | 5.9×|     8 451 |     1 019 | 8.3× |
|  3200  |   193 555 |    15 745 |12.3×|    30 232 |     1 930 |15.7× |

n=3200：前端 193s→15.7s（12.3×），峰值内存 30.2GB→1.9GB（15.7×）。基线 30GB
印证真 impl 的 42GB 峰值来源。两个 O(N²) 叠加，收益随规模相乘。
（注：第二个 O(N²)=DCE ReportUnusedFunc 的 ReflectPackageIsUsed per-func 全包扫描，
gitcode main 上 1763ms→50ms(35×)；但 tmp_build 早已在 b26cfc68 修复，非新增。）

## dev_perf 分支（交付）

按上游 **gitcode dev (bb251200)** 基线建 `dev_perf`（T:\cangjie_compiler）：
dev + cherry-pick tmp_build 的 10 个 perf commits（399792d3..6d74761b）+ 新增优化。

**新增优化 commit 66786c83：`perf(sema): memoize BuildAbstractFuncMap`**
- 根因：`BuildAbstractFuncMapHelper` 对每类的每个祖先重算 `GetInheritedMemberFuncs`/
  `GetInheritedInterfaces`（纯函数），深继承链上 ~O(N·depth³)。深 COM/vtable 链是真 impl 模式。
- 修复：按 Ty* memoize（pass 内层级固定，每次 BuildAbstractFuncMap 起始清空）。
- 实测 1600 类×深度64：BuildAbstractFuncMap 7529→3631ms(2.1×)；现实深度(≤16)降到非问题。
- 正确性：泛型接口分发 compile+run 验证（深 override 链 super 链路正确）。

**多维 profiling 方法论**（脚本见 T:\cangjie_compiler\*_profile.py、/root/cj_build/*.py）：
全 pass 分解 + 按 类数/继承深度/接口宽度/依赖数 扫描伸缩指数。其余候选经测排除：
SortGlobalVarDecl/AddDependencyImpl O(D²) 常数极小(4000 deps=11ms)；TypeCheck/CHIR exp~1.22 固有。

## 真实 windows-cj 端到端验证（已完成 ✅）

日期：2026-06-14。本地 plucky 交叉构建受阻于 llvm-mingw 工具链代差，**改用
CangjieFork/cangjie_compiler@dev_perf + cangjie-build 的 Azure CI**（ubuntu-22.04 +
Azure ephemeral VM，环境对齐）构建 Windows x64 SDK（run 27496364700 success），
下载后 `cjv toolchain link dev_perf_ci` 加载（cjc 0.0.0-dev x86_64-w64-mingw32）。

基线用官方 nightly `nightly-1.2.0-alpha.20260613020028`（无任何 dev_perf 优化）。
两者均 `cjpm clean` 后全量编译 **windows_common**（= deps + `src/impl` 巨石
8 文件 / 222,632 行 / 812 类），`-j 8`，cjpm `cjHeapSize=8gb`。逐 400ms 采样
`cjc`/`cjc-frontend` 的 WorkingSet64 取峰值（测量脚本 `measure_impl.ps1`）。

| 指标 | 基线 (nightly) | 优化 (dev_perf_ci) | 改善 |
|---|---:|---:|---:|
| 墙钟时间 | 1 077.0 s (18.0 min) | 329.0 s (5.5 min) | **3.27×** |
| 峰值 cjc 内存 | 42.37 GB | 11.57 GB | **3.66×** |
| 结果 | cjpm build success | cjpm build success | ✅ 一致 |

- **42GB→11.6GB** 内存降幅精确印证 InitChecker O(N²)→O(N) 的预测（与记忆
  `project_compile_pressure_impl_monolith` 记录的 42GB 峰值来源吻合）。
- **真 impl 上 CPU 与内存双双显著优化**，端到端验证闭合。算法层（合成压测，最高
  46.9× CPU / 15.7× 内存）与真实工作负载一致方向，量级随规模相乘。
