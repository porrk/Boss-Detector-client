# 模板聚合功能 · 实现方案（定稿 v3）

> 状态：**已通过审查，开始实现。**

## 最终决策
| 项 | 决策 |
|---|---|
| 议题1 | score 加权平均聚合；一人一行聚合体；新增与聚合体相似度过低弹窗确认 |
| 议题2 | 新增人员时姓名查重；重叠则弹窗选「同人聚合 / 另一人改名」 |
| 议题3 | B：不分组，按姓名排序 + 状态栏统计人数/条数 |
| 议题4 | 自动消解（一人一行，只有一个 alert_level） |
| 议题5 | 「从检测入库」并入 TemplateDialog（预填 embedding，跳过选图/检测） |
| 议题6 | 双向检查：sim<0.20 警告(可能贴错人) / sim>0.95 提示(可能重复) + 软上限 10 |
| 议题7 | 不引入 person 表 |
| N1 | b：按检测分数加权 `A=norm(Σ score_i·emb_i)` |
| N2 | 0.20 |
| N3 | b：新建 `template_samples` 表存原始 embedding |
| N4 | a：拒绝聚合则丢弃（不入库） |
| N5 | 姓名唯一约束 |
| N6 | 撞脸检查，阈值 0.50 |

## Schema
- `templates`：`name` 加 UNIQUE（迁移时合并同名）；`embedding` 列存聚合体；不加新列。
- `template_samples(id PK, template_id INT, embedding TEXT, score REAL, created_at INT)`，索引 `template_id`。

## 迁移（幂等，启动时执行）
1. 建 `template_samples` 表。
2. 回填：每个无 sample 的 template 插入一条 sample(score=1.0, embedding=该行 embedding)。
3. 合并同名：同名多行 -> 保留 min id，等权聚合为 embedding，samples 归并到保留行，删多余 template 行。
4. `DROP INDEX IF EXISTS idx_templates_name; CREATE UNIQUE INDEX idx_templates_name ON templates(name);`

## 工具函数（放 db.h/.cpp）
- `cosSim(a,b)` = dot/(|a||b|)
- `weightedAggregate(samples)` = normalize(Σ score_i·emb_i)，Σscore≈0 退化为等权，仍为0返回空

## 流程
**归入现有人员 X：**
1. 取 X 的 template（聚合体 A）+ sampleCount n。
2. s = cosSim(x, A)。
3. s<0.20 -> 弹窗「相似度仅 {s:.2f}，可能不是同一人，是否仍然聚合？」取消则丢弃(N4)。
4. s>0.95 -> 弹窗「高度相似，可能重复录入，是否仍入库？」取消则丢弃。
5. n>=10 -> 弹窗「已有 {n} 条样本，是否继续？」取消则丢弃。
6. 确认：addSample(X.id, x, score) + recomputeAggregate(X.id)。

**新增人员：**
1. 键入姓名 -> 姓名查重：重叠则弹窗「同人(聚合) / 另一人(改名)」。同人->走聚合；另一人->留对话框改名。
2. 无重叠 -> 撞脸检查：max sim(x, 各聚合体) > 0.50 则弹窗「这张脸像 '{best}'(sim)，是否同人？」是->走聚合；否->新增。
3. 新增：db.add(template) + addSample(newId, x, score)。

**从检测入库（议题5）：** TemplateDialog 预填 embedding+score，隐藏选图/检测，走新增/聚合流程。

## 文件改动
1. `config.h/.cpp` + `assets/default_config.ini`：4 阈值字段
2. `db.h/.cpp`：schema/迁移/方法(`listDistinctNames`,`getByName`,`countSamples`,`addSample`,`listSamples`,`recomputeAggregate`,`replaceSamples`)/`cosSim`/`weightedAggregate`
3. `template_dialog.h/.cpp`：模式切换+姓名下拉+查重+撞脸+低/高弹窗+软上限+预填入口
4. `main_window.cpp`：`onAdd` 传去重姓名；`refreshTable` 按 name 排序+统计；`onSaveDetectionAsTemplate` 改走预填 TemplateDialog
5. `face/recognition.cpp`：不改

## 阈值（config.ini `[recognition]`）
```
aggregate_low_threshold=0.20    ; 新特征与聚合体相似度低于此值警告
aggregate_high_threshold=0.95   ; 高于此值提示重复
face_match_threshold=0.50       ; 新增时撞脸检查阈值
aggregate_soft_cap=10           ; 每人样本数软上限
```
