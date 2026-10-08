# AI Workflow

## Highest-priority Rule

除经 Human 授权的重大架构审计外，GPT 和 DeepSeek 不应该为了普通功能开发重新完整扫描 Repository。
必须首先：Project Docs → 定位模块 → 请求最小必要源码 → 完成当前 TASK。
本次首次 Baseline 建立需要审计实际源码；后续普通需求不得把此次范围作为默认权限。

## Roles

GPT = Tech Lead / Architect / Reviewer：需求澄清、影响分析、TASK、架构维护、审阅和 FIX/PASS。
DeepSeek = Coding Agent：按 TASK 实施最小改动、真实验证、RESULT 和 diff。
Human = Product Owner / Final Approver：确定需求/验收、批准范围与最终提交。GPT Review PASS 不代替 Human Approval。

## Workflow

Requirement → GPT Analysis → TASK → DeepSeek Implementation → RESULT → git diff → Build/Test → GPT Review → FIX or PASS → Human Approval → Commit → Update PROJECT_STATE。

实施阶段产生 RESULT 草稿和 diff；Build/Test 后补充 RESULT 的真实证据；GPT 审阅同一代码版本。FIX 回到实施并重复受影响验证；最终 PASS 后由 Human 批准。审批后代码变化须重新审阅。
Human 明确授权自动提交可作为本任务提交授权；未明确授权时在具体 diff 和检查结果准备完成后请求。
业务提交后更新 PROJECT_STATE，并单独提交状态更新（不无限递归更新文档提交历史）。首次基线五文件可在统一核对后同一提交落库，不把提交成功预写为事实。

## Project Docs Reading Order

优先 PROJECT_STATE.md → PROJECT.md → ARCHITECTURE.md → CODING_RULES.md → 本文件。
稳定事实以 PROJECT 为入口，当前问题以 STATE 为入口；具体实现以源码为准。文档冲突先报告并限定读取相关源码。

## TASK File Format

建议路径 docs/tasks/<ID>/TASK.md；只有任务需要时创建，不为本次额外生成。
```markdown
# TASK <ID>: <标题>

## Requirement

用户需求、背景、预期行为。
## Scope

允许修改的文件、模块、入口与明确不在范围内的工作。
## Context

基线依据、已提供源码、缺失上下文及相关现有行为。
## Impact Analysis

公共接口/数据结构/配置/调用方/线程/输出格式影响。
## Implementation Constraints

最小方案、需保留的逻辑、风险与授权。
## Acceptance Criteria

可观察行为、参考数据、数值容差与失败条件。
## Build/Test Plan

真实命令、工具环境、样本、检查项及预期结果。
## Documentation Updates

需更新哪些基线文档及理由。
## Approval

Human 范围授权、提交授权及待确认事项。
```
上下文不足或验收标准冲突时先报告；不得凭空填入参考值。

## RESULT File Format

建议与 TASK 同目录 RESULT.md。
```markdown
# RESULT <ID>

## Status

IMPLEMENTED / BLOCKED / NEED_CONTEXT（不是审阅 PASS）。
## Changes and Reasons

逐文件记录实际修改与原因。
## Contract and Risk

接口/数据结构/行为变化及剩余风险。
## Diff

基准版本、实际 git diff 命令与完整 diff 或可审阅附件。
## Build/Test

逐项命令、环境、退出码、实际输出、PASS/FAIL/NOT_RUN 与原因。
## Deviations

与 TASK 的差异、缺失上下文与需确认的问题。
## Documentation

更新文档和证据；尚未更新项及原因。
```
无 Git 时明确标记 diff/版本不可用，不虚构；新仓库首次提交用暂存 diff 审阅。

## REVIEW File Format

建议与 TASK 同目录 REVIEW.md。
```markdown
# REVIEW <ID>

## Verdict

PASS / FIX / NEED_CONTEXT。
## Reviewed Version

基准和待审版本或暂存 diff 身份。
## Scope and Evidence

所读文档、最小源码、diff、实际 Build/Test 证据。
## Findings

文件/函数/位置、问题、影响、必要修复与验证。
## Acceptance

逐条验收满足情况；未执行项和剩余风险。
## Documentation Consistency

基线更新是否准确。
## Human Approval

待审批或明确授权引用；实际提交后才能记录提交号。
```
代码审阅 PASS、构建 PASS、数值验收 PASS 和基线 READY 是不同结论，必须分别报告。

## Reading Additional Source

允许读取 TASK 目标模块、直接调用者/被调用者、相关接口/结构、配置、构建定义和验收代码。
缺上下文先写明：具体文件/函数、要解决的问题和为什么需要；仅获取该范围。
公共契约变更可在限定模块搜索调用引用，按结果扩展；不可因不确定就全仓遍历。
发现未知实现或 TASK 有误时停止依赖该假设的实施并报告。扩大架构审计须由 Human 明确授权。

## Updating PROJECT_STATE

已完成功能、当前工作、已知缺陷/债务、决策、限制、优先级发生实质变化，以及业务提交后更新。
保留短快照；完成项写结果和关键限制，临时日志放 RESULT；不复制 Git 历史。未知写 UNKNOWN，产品/授权待定写 NEED USER CONFIRMATION。

## Updating ARCHITECTURE

模块职责/依赖、入口/调用链、公共接口/结构、线程、内存所有权或配置/错误流改变时更新。描述落地实现，计划放 TASK；不将理想架构写成事实。

## Updating PROJECT

平台、语言、依赖、构建/运行/测试方法或长期目标改变时更新。临时失败和机器当前状态写 STATE/RESULT。

## Updating CODING_RULES

Human 批准的约束或明确形成的团队规范改变时更新；观察到新风格不自动推翻既有规范。不得通过改规则绕过当前 TASK 约束。

## Baseline Consistency Check

五文件落库前逐项检查：事实来源与源码一致；无相互矛盾；重复内容改为引用；临时状态未混入 PROJECT；推断与未执行测试明确标注；STATE 只保留下一任务所需信息。
仅有明确标注且不影响当前基线事实的后续产品待定项，可以报告 BASELINE_READY；无影响落库的待定事项报告 BASELINE_READY；有需要 Human 决定的事项报告 BASELINE_NEEDS_CONFIRMATION 并逐项列出，在确认前保留草稿，不正式落库。
每次更新检查受影响文档；重大审计检查全部五份。Commit 必须遵守明确授权与环境权限，不伪造执行结果。
