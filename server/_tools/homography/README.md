# homography_tool

Standalone OpenCV utility. It is built only with `-DBUILD_HOMOGRAPHY_TOOL=ON`
and does not link any server library.

Grid configuration:

```json
{
  "dictionary": "DICT_4X4_50",
  "cols": 4,
  "rows": 3,
  "marker_len_cm": 4.0,
  "gap_cm": 2.0,
  "id_offset": 10,
  "origin_corner": "TL"
}
```

IDs are `row * cols + col + id_offset`; marker corners are TL, TR, BR, BL.
The origin is the TL corner of the top-left marker, with X right and Y down,
in centimetres.

Examples:

```sh
homography_tool gen-board --config grid.json --output board.png
homography_tool gen-board --config grid.json --output board.svg \
  --board-width-mm 480 --board-height-mm 1065 --margin-mm 15
homography_tool calibrate --config grid.json --input capture.png \
  --output homography_ch1.json --channel 1 --max-rmse-cm 2
homography_tool view --config grid.json --homography homography_ch1.json \
  --input capture.png --output-dir view
homography_tool selftest --verbose
```

CTest로도 실행할 수 있습니다.

```sh
cd build-homography
ctest -R homography_selftest --output-on-failure
```

`gen-board` accepts `.svg` and `.png`. SVG is recommended for physical
printing because its physical width and height are explicitly recorded in mm;
print it at 100% / actual size. PNG uses the requested `--dpi` (default 300)
to calculate its pixel dimensions. The default physical output hides IDs and
grid guides; TL reference crosses are placed outside each marker. Use
`--show-ids` or `--show-grid` for inspection output, and `--no-origin` to hide
the reference crosses.
