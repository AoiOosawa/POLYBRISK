# POLYBRISK ユーザーガイド

POLYBRISK は、SuperSAW・Synth Brass・Stab を速く作るための macOS 用ポリフォニック・シンセサイザーです。
このガイドは開発中のビルド（v0.1.0）に対応しています。

> **お読みください**: このビルドは未リリースです。自動テストと検証ツール（auval / pluginval）は通っていますが、DAW 上での動作確認はまだ済んでいません（収録音色は Standalone で一通り試聴しています）。

## 1. 動作環境

| 項目 | 内容 |
|---|---|
| OS | macOS 13 以降 |
| CPU | Apple Silicon |
| 形式 | Audio Unit（Logic Pro など）、VST3（Cubase など）、Standalone アプリ |
| 入出力 | MIDI 入力、ステレオ出力（音声入力なし） |
| サンプルレート | 44.1 / 48 / 88.2 / 96 kHz |

Cubase は Audio Unit に対応していないため、Cubase では VST3 を使います。

## 2. インストール

インストーラー（`.pkg`）を実行すると、次の場所に入ります。

| 形式 | 場所 |
|---|---|
| Audio Unit | `/Library/Audio/Plug-Ins/Components/POLYBRISK.component` |
| VST3 | `/Library/Audio/Plug-Ins/VST3/POLYBRISK.vst3` |
| Standalone | `/Applications/POLYBRISK.app` |

インストール後、DAW でプラグインを再スキャンしてください。メーカー名は **CHEEBOW**、種類はインストゥルメントです。

アンインストールは、上の 3 つを削除します。ユーザープリセットと設定は `~/Library/Application Support/POLYBRISK/` にあります。

## 3. 最初の音を出す

1. DAW でインストゥルメントトラックを作り、POLYBRISK を挿します（Standalone ならアプリを起動し、Options からオーディオ / MIDI デバイスを選びます）。
2. 画面上部のプリセット名をクリックしてブラウザーを開き、音色を選びます。`<` `>` で前後の音色に移れます。
3. 鍵盤を弾きます。Standalone では画面下の鍵盤をクリックしても鳴ります。鍵盤の左にある **BEND**（ピッチベンド。上下にドラッグし、離すと中央へ戻ります）と **MOD**（モジュレーション。ビブラートが掛かり、離した位置に残ります。ダブルクリックで 0）は、マウスで動かせます。外部の MIDI キーボードのホイールを動かすと、画面のホイールも一緒に動きます。鍵盤は 88 鍵です。

## 4. 画面の構成

上部のバーは常に表示されます。

| 部品 | 働き |
|---|---|
| `<` `>` | 前 / 次のプリセット |
| プリセット名 | クリックでブラウザーを開く（もう一度クリック、Close、または SIMPLE / ADVANCED を押すと閉じる）。変更があると名前の後ろに `*` が付く |
| Save | User プリセットなら、読み込んだファイルへ上書き保存。Factory や未保存の音色、または上書きに失敗したときは Save As を開く |
| SIMPLE / ADVANCED | 表示の切り替え。**切り替えても音は変わりません**（同じ設定を 2 通りに見せているだけです） |
| OUT | 全体の出力音量。**プリセットを切り替えても変わりません**（音色には含まれません）。DAW のプロジェクトには保存されます。スライダーの下の 2 本の細い線は出力メーター（上が左、下が右）で、赤くなり `LIMIT` と出たら、音が大きすぎて丸められています |

ウインドウの右下をドラッグすると、縦横比を保ったまま拡大縮小できます。

### ノブの操作

- ドラッグで変更、**Shift を押しながら**で微調整
- **ダブルクリック**で初期値に戻す
- 値の表示をクリックして数値を直接入力

## 5. Simple 表示

普段はここだけで足ります。

- **MACROS**: 4 つの大きなノブ。名前と効き方は**プリセットごとに違います**（例: Bright、Detune、Space、Release）。中央（50 %）が音色の基準の状態です。
- **TONE**: 音色を選んだあとによく直す 4 つのノブ。**Cutoff**（明るさ）、**Resonance**（フィルターの癖の強さ）、**Attack**（立ち上がりの速さ）、**Release**（鍵盤を離したあとの余韻の長さ）。Advanced の Filter / Amp にある同じ設定で、どちらで動かしても同じです。マクロが Cutoff などを動かす音色では、このノブが基準の位置を決め、マクロがそこから上下させます。
- **FX**: 7 種類のエフェクトの ON/OFF と、まとめて切る **FX Bypass**。Bypass 中は各スイッチの色が落ち、`BYPASS` と表示されます（ON の設定は残ります）。
- **RHYTHM**: **GATE**（音を刻む）と **ARP**（和音を分散させる）。ON にすると、最低限の設定が現れます。
- 下部に、ピッチベンド幅とビブラートの設定、プリセットの説明が表示されます。

## 6. Advanced 表示

6 つのタブで、すべての設定を編集できます。

### OSC
OSC A と OSC B は同じ構成です。

| 設定 | 内容 |
|---|---|
| Wave | Saw / SuperSAW / Pulse / Square / Triangle / Sine / Noise |
| Octave / Semitone / Fine | 音程（Noise では表示されません） |
| Level | 音量 |
| Unison | 重ねる本数（1〜9）。SuperSAW を選ぶと、1 本のときは自動で 7 本になります |
| Detune / Spread | 重ねた音のずれと、左右の広がり |
| Uni Mix | 重ねた音のうち、中央の音と外側の音の音量比。50 % で全部同じ、下げると芯がはっきりし、上げると外側が前に出て芯が薄くなる（Unison が 3 以上のとき） |
| Phase | Free（毎回違う位相）/ Retrigger（毎回同じアタック。Stab 向き） |
| Pulse Width | Pulse を選んだときだけ表示 |

**Transient** は、アタックに短い音を足します。Noise Burst（ノイズ）、Pitch Snap（音程が指定の高さから落ち着く）、Both。

**Sub / Noise** は、OSC A / B とは別に足す 2 つの層です。

- **Sub**: 弾いた音の 1 または 2 オクターブ下を、中央に 1 声で足します（Sine / Square）。OSC の Octave や Semitone を変えても、Sub は弾いた音の下に居続けます。
- **Noise**: 鍵盤を押さえているあいだ鳴り続けるノイズです。Color で暗い ↔ 明るいを変えます（Transient のノイズはアタックだけ）。

どちらも 0 % なら鳴りません。

### Filter & Mod

| 設定 | 内容 |
|---|---|
| Character | **Analog**（温かく、軽く歪む）/ **Clean**（癖がない）/ **Punch**（輪郭が出る、レゾナンスの音量を抑える） |
| Mode | LP12 / LP24 / HP12 / HP24 / BP12 |
| Cutoff / Resonance / Drive | 明るさ、ピークの強さ、フィルター内の歪み |
| Env Amount | Filter Envelope がカットオフを動かす量（マイナスにもできます） |
| Key Track | 高い音ほどカットオフを上げる量 |
| Velocity | 弱く弾いたときに暗くする量 |

Filter Envelope と Amp Envelope は Attack / Decay / Sustain / Release。Amp の **Velocity** は、弾く強さで音量が変わる度合いです。Amp の Sustain を 0 % にすると、鍵盤を押さえたままでも音が消えた時点で発音を終えます。ノブの下の図は、いまのエンベロープの形です（表示のみ）。

**LFO** は、音を周期的に揺らします。1 基で、行き先は次の 4 つに決まっています。それぞれのノブが 0 なら効きません。

| 設定 | 内容 |
|---|---|
| Wave | Sine / Triangle / Saw / Square / S&H（周期ごとにランダムな値） |
| Sync / Rate | Sync が OFF なら Hz、ON ならテンポに合わせた長さ（4 Bars 〜 1/16） |
| Fade In | 弾いてから揺れが深くなるまでの時間 |
| Key Sync | ON: 弾くたびに揺れの頭から始まる。OFF: すべての音が同じ揺れを共有する |
| > Cutoff | フィルターの開閉（マイナスにすると逆向き） |
| > Pulse W | Pulse 波の幅（PWM）。Wave が Pulse の OSC にだけ効きます |
| > Pitch | 音程の揺れ |
| > Amp | 音量の揺れ（トレモロ） |

Mod Wheel のビブラートとは別に動きます。

### Performance

- **Pitch Bend**: Symmetric（上下同じ幅）/ Independent（上下別々）。幅は 0〜48 半音。
- **Vibrato**: モジュレーションホイール（CC1）で掛かります。Depth（最大の深さ）、Rate（速さ）、Delay（弾いてから掛かり始めるまでの時間）。
- **Voice**: Mode は **Poly**（和音）/ **Mono**（1 声。弾くたびにアタックし直す）/ **Legato**（1 声。鍵を押さえたまま次を弾くと、アタックし直さずに音程だけ移る）。**Glide** は音程が滑らかに移る時間です。Legato では、つなげて弾いたときだけ滑ります。
- **Aftertouch**: 鍵盤を押し込む力（チャンネルプレッシャー）で、ビブラート（> Vibrato）とフィルター（> Cutoff）を動かします。0 なら効きません。
- **Preset（Level）**: その音色の音量の補正（−24〜+24 dB）。プリセットに保存されます。自分で作った音色がほかより大きい / 小さいときは、上部の OUT ではなくこちらで合わせます。Factory の音色は、ここで音量をそろえてあります。

### FX
信号は Drive → Phaser / Flanger → Chorus → EQ → Imager の順に流れ、その後で Delay と Reverb が並列に掛かります。

| FX | 設定 |
|---|---|
| Drive | Type（Warm / Tape / Tube / Soft Clip / Hard Clip / Wavefold）、Drive、Tone、Mix、Output、Auto Gain |
| Phaser / Flanger | Type（Phaser / Flanger）、Rate、Depth、Feedback、Mix（100 % で効果が最大） |
| Chorus | Type（Classic / Wide）、Rate、Depth、Width、Mix |
| EQ | Low（100 Hz）、Mid（周波数とゲイン）、High（8 kHz） |
| Stereo Imager | Width（100 % が元の幅、0 % でモノラル）、Mono Bass（低域を中央に寄せる） |
| Delay | Mode（Stereo / Ping Pong）、Sync とそのときの Division、Sync を切ったときの Time、Feedback、Tone、Mix、Ducking（弾いている間はディレイを下げる） |
| Reverb | Type（Room / Plate / Hall）、Size、Decay、Pre-Delay、Tone、Mix |

Delay と Reverb の Mix は、50 % までは原音をそのままにしてエフェクト音を足し、50 % を超えると原音が下がります。

### Rhythm

**Trance Gate** は、音量をステップのパターンで刻みます。

- グリッドの**クリック**でステップの ON/OFF、**上下ドラッグ**で高さ（音量）を変えます。
- Steps（8 / 16 / 32）、Rate（1 ステップの長さ）、Depth（刻みの深さ）、Smooth（切り替わりのなめらかさ）、Swing。
- Restart: **Transport** は DAW の小節に合わせ、**Note On** は弾いた瞬間からパターンを始めます。
- Rate は 1/8 / 1/16 / 1/32 / 1/64 と 3 連（1/8T / 1/16T）。
- **Random** は、ランダムなパターンを作ります（Stereo が ON なら左右別々）。気に入らなければ **Undo** で 1 つ前に戻せます。
- **Stereo** を ON にすると、グリッドが上下 2 段（上が左、下が右）になり、左右で別のパターンを組めます。**Width** を下げると左右の差が小さくなり、0 % では左右とも同じ動きになります。メニューの Stereo グループのパターンを選ぶと、Stereo は自動で ON になります。パターンを選ぶと、Rate もそのパターンに合う値になります。Fast 1/32 グループは 1/32（Chop Tail は 1/64）、それ以外は 1/16 です。選んだあとで Rate を変えるのは自由で、**Undo** はパターンと Rate を一緒に戻します。最初の状態は Rate 1/32・パターン Shifted Long（16 分の刻みに長い音が 2 つ混ざる形）です。
- パターンのメニューから 52 種類のパターンを選べます（Basic / Straight / Trance / Fast 1/32 / Stereo / Syncopated / Accent / Fill）。ON のステップが続くところは途切れずにつながり、長い音になります。選び直すと編集した内容は置き換わりますが、**Undo** で 1 回だけ戻せます。
- Gate は Delay / Reverb の手前にあるので、刻んだ音の残響は自然に残ります。FX BYPASS では切れません。

**Arpeggiator** は、押さえた和音を 1 音ずつ順に鳴らします。

- Mode: Up / Down / Up-Down / Random / As Played（押した順）
- Rate、Octaves（1〜4）、Gate Length（1 音の長さ）、Swing、Velocity
- **Latch**: 鍵盤を離しても鳴り続けます。新しい和音を弾くと入れ替わります。止めるには Latch を切ります。
- Arp が ON の間、サステインペダルは「和音を保持する」働きになります。
- DAW を停止すると、Latch で保持していた和音は消えます。

### Macros
マクロ 1〜4 それぞれについて、次を設定できます。

- **名前**: 上の欄に入力して Return。
- **MIDI Learn**: 押してから MIDI コントローラーのつまみを動かすと、そのコントロールチェンジが割り当たります。割り当てられるのは CC2〜119（サステインの CC64 を除く）です。CC1 はビブラート専用です。
- **割当**: 「+ Add target」で行を足し、対象のパラメーターを選びます。**LOW** はマクロを 0 % にしたとき、**HIGH** は 100 % にしたときに、そのパラメーターをどれだけ動かすか（範囲に対する ±%）です。マクロが 50 % のときは何も動かしません。
- 1 つのマクロに最大 8 個まで。複数のマクロを同じパラメーターに割り当てると、動かす量が足し合わされます。
- 割り当てられるのは連続的に変わるパラメーターだけです（Wave や ON/OFF は対象外）。

マクロが動かすのは「基準の値からのずれ」なので、マクロを回したまま Advanced でノブを動かしても、値が飛ぶことはありません。

## 7. プリセット

ブラウザーでは次のことができます。

| 操作 | 内容 |
|---|---|
| 検索 | 名前・説明・タグ・カテゴリから探す（複数の語はすべてを含むものに絞る） |
| カテゴリ / Favorites | カテゴリやお気に入りで絞り込む |
| タグのボタン | 音色に付いているタグ（Bright、Warm、Gate など）で絞り込む。複数押すと、そのすべてを持つ音色だけが残る。**Clear** で解除。検索・カテゴリ・Favorites と組み合わせられる。自分で保存した音色のタグもここに並ぶ |
| 行の左端の ★ | クリックでお気に入りに入れる / 外す（音色は切り替わりません） |
| Init | 空の音色から作り始める。すべての設定を既定値（OSC A のソー 1 本、フィルター全開、FX なし）に戻し、マクロの割当を消す。OUT は変わらない。一覧の音色ではないので、作った音は Save As で保存する |
| Save As... | 名前・カテゴリ・タグ・説明を付けて User プリセットとして保存。同じ名前の User プリセットがあれば上書きします |
| Delete | いま読み込んでいる User プリセットを削除（Factory は削除できません）。取り消せないので、1 回目はボタンが赤い **Click again to delete** に変わり、もう一度押すと消えます（ほかの音色を選ぶか、ブラウザーを閉じると取り消されます） |
| Import... / Export... | プリセットファイル（.json）の読み込み / 書き出し。取り込んだプリセットは User フォルダーにコピーされ、別の音色として扱われます（同じファイルを 2 回取り込めば 2 つになります） |
| Performance Lock | ON にすると、プリセットを切り替えてもピッチベンド幅とビブラートの設定を保ちます |

収録している Factory プリセットは 128 音色です。**Trance Gate** カテゴリの音色は、和音を押さえたままにすると Gate がリズムを刻みます（DAW を再生していなくても、Standalone でも動きます）。

| カテゴリ | 数 |
|---|---|
| SuperSAW | 11 |
| Synth Brass | 12 |
| Stab / Chord Hit | 16 |
| Bright Poly | 7 |
| Synth Pluck | 7 |
| Lead / Hook | 12 |
| Bass | 7 |
| Pad / Strings | 10 |
| Octave / Layered | 3 |
| Trance Gate | 18 |

DAW のプロジェクトには、プリセット名だけでなく**すべての設定**が保存されます。プリセットファイルを消しても、プロジェクトの音色は変わりません。

## 8. MIDI

| MIDI | 働き |
|---|---|
| ノート | 最大 16 音。足りなくなると、消えかけの小さい音、次に最も古い音から止めます |
| ピッチベンド | Performance タブの幅で上下 |
| CC1（モジュレーション） | ビブラートの深さ |
| アフタータッチ | Performance タブの Aftertouch の量だけ、ビブラートとフィルターに効く |
| CC64（サステイン） | 音を伸ばす（Arp が ON のときは和音の保持） |
| CC2〜119（64 を除く） | MIDI Learn でマクロに割り当て可能 |
| CC120 / CC123 | すぐに消音 / すべての音をリリース |

MPE には対応していません。MIDI チャンネルは区別しません。

## 9. DAW との連携

- Delay の Sync、Trance Gate、Arpeggiator は DAW のテンポに合います。テンポが得られない場合は 120 BPM で動きます。
- マクロをはじめ、すべてのパラメーターをオートメーションできます。
- Gate のパターンそのものはオートメーションの対象ではありません（プリセットとプロジェクトに保存されます）。

## 10. 困ったとき

| 症状 | 確認すること |
|---|---|
| 音が出ない | OSC A / B の ON、Level、Cutoff が極端に低くないか、Gate が閉じたパターンになっていないか、OUT の値 |
| プラグインが一覧に出ない | DAW の再スキャン。Cubase では VST3 フォルダー、Logic では Audio Unit の検証結果を確認 |
| Arp が止まらない | Latch を切るか、DAW を停止する |
| DAW のキー操作（スペースキーでの再生など）が効かない | POLYBRISK は、文字の入力欄（検索、Save As、マクロ名、ノブの数値入力）にカーソルがある間だけキーを受け取ります。Esc か Return で入力を終えるか、入力欄の外をクリックすると、キーは DAW に戻ります |
| 音が歪む | OUT の下のメーターに `LIMIT` が出ていたら、OUT を下げる。Drive や Filter Drive、Resonance を下げる。出力には安全用のリミッターがあり、大きすぎる音は丸められます |
| 高音がこもる | Filter の Cutoff を上げる。Drive の Tone、Delay / Reverb の Tone を確認 |

## 11. 現在の制限

- Windows、AAX、CLAP、AUv3、Intel Mac には対応していません。
- MPE、自由なモジュレーションマトリクスはありません。LFO は 1 基で、行き先は 4 つに固定です。アフタータッチは鍵盤全体で 1 つの値として効きます。
- Mono / Legato で音を伸ばした余韻（Release）は、次の音を弾いても切れずに重なります。
- Arpeggiator の音を MIDI として外へ出すことはできません。
- フィルターの Character / Mode を音を出したまま切り替えると、小さなノイズが出ることがあります。
