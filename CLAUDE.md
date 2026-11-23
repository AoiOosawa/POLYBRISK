# Claude Code 開発指示 — POLYBRISK

## 目的
`SPEC.md` に記載した SuperSAW / Synth Brass / Stab 特化の macOS AU/VST3 シンセを、段階的に開発する。

## 最重要ルール
1. **最初に `SPEC.md` 全体を読む。** `SPEC.md` を製品要件の正本とする。追加仕様 `docs/ADDON_TRANCE_GATE_ARPEGGIATOR.md`（Trance Gate / Arpeggiator）も読み、衝突する箇所は追加仕様を優先する。
2. いきなりすべてを実装しない。`SPEC.md` の Phase 0 → Phase 7 の順に進む（Phase 4 と 5 の間に Phase 4.5 — Rhythm）。
3. 各フェーズで **ビルド可能な状態、テスト、動作手順** を必ず残す。
4. オーディオコールバックでメモリ確保、ロック待機、ファイルI/O、JSONパースをしない。
5. DSPとUI/プリセット管理の依存関係を分離する。
6. StableなParameter IDを一か所で管理し、ホストオートメーションと状態復元を壊さない。
7. 仕様に曖昧さがある場合、SPECの`[暫定]`を初期値として使い、`docs/DECISIONS.md`に採用した判断を記録する。
8. 128プリセット（当初の目標は256。D-060で変更）を単純な数値差分やダミーデータで水増ししない。
9. 失敗したコマンド・未テスト項目は隠さず報告する。
10. JUCEの採用バージョン、ライセンス、Xcode/CMake環境は実装開始時に検証する。

## 開始時に実行すること
- 現在のリポジトリと開発環境を検査する。
- `docs/ARCHITECTURE.md`、`docs/PARAMETER_IDS.md`、`docs/DECISIONS.md`、`docs/TEST_PLAN.md` を作成・更新する。
- Phase 0 の作業計画と受け入れテストを確認して着手する。
- `juce_add_plugin` を使いAU/VST3 Instrumentがビルドできるようにする。
- MIDIノートで音が出て、DAWのプロジェクトを保存復元できるまでを最初の完了条件とする。

## 変更ルール
- 仕様改訂が必要なら `SPEC.md` の関連項目と `docs/DECISIONS.md` をセットで更新。
- パラメーターの範囲・デフォルト変更では既存プリセットの互換性を評価。
- コミット/PRメッセージにはPhaseとテスト結果を含める。

## 各フェーズ終了時の報告形式
- 実装したもの
- 変更ファイル
- 実行したビルド/テストと結果
- DAWでの実機確認事項と未確認事項
- 次フェーズの作業候補
