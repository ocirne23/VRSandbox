# Third-Party Assets

Everything except the terrain-diffusion model weights is licensed **CC0 1.0 Universal** (public domain
equivalent) — free for any use, including commercial, with no attribution required. Credit is listed anyway
for provenance. The model weights are **MIT**, which does require the copyright notice to be preserved —
see [Terrain Diffusion models](#terrain-diffusion-models--assetsterraindiffusion) below.

## Vegetation / Rocks — `Assets/Models/PolyHaven/`

Source: [Poly Haven](https://polyhaven.com) ([license](https://polyhaven.com/license))

| Asset | Link |
|---|---|
| Namaqualand Rocks 01 | https://polyhaven.com/a/namaqualand_rocks_01 |
| Namaqualand Boulder 04 | https://polyhaven.com/a/namaqualand_boulder_04 |
| Stone 01 | https://polyhaven.com/a/stone_01 |
| Tree Small 02 | https://polyhaven.com/a/tree_small_02 |

## Terrain Textures — `Assets/Textures/Terrain/`

### Poly Haven ([license](https://polyhaven.com/license))

| Folder | Asset | Link |
|---|---|---|
| `aerial_sand/` | Aerial Sand | https://polyhaven.com/a/aerial_sand |
| `dry_ground_01/` | Dry Ground 01 | https://polyhaven.com/a/dry_ground_01 |
| `mud_forest/` | Mud Forest | https://polyhaven.com/a/mud_forest |
| `forest_floor/` | Forest Floor | https://polyhaven.com/a/forest_floor |
| `forest_ground_04/` | Forest Ground 04 | https://polyhaven.com/a/forest_ground_04 |
| `rocky_trail/` | Rocky Trail | https://polyhaven.com/a/rocky_trail |
| `snow_02/` | Snow 02 | https://polyhaven.com/a/snow_02 |
| `brown_mud_03/` | Brown Mud 03 | https://polyhaven.com/a/brown_mud_03 |
| `rock_face/` | Rock Face | https://polyhaven.com/a/rock_face |
| `gray_rocks/` | Gray Rocks | https://polyhaven.com/a/gray_rocks |
| `terrain_red_01/` | Terrain Red 01 | https://polyhaven.com/a/terrain_red_01 |
| `dark_rock/` | Dark Rock | https://polyhaven.com/a/dark_rock |

### ambientCG ([license](https://docs.ambientcg.com/license/))

| Folder | Asset | Link |
|---|---|---|
| `Grass001/` | Grass 001 | https://ambientcg.com/view?id=Grass001 |
| `Moss002/` | Moss 002 | https://ambientcg.com/view?id=Moss002 |

## Terrain Diffusion models — `Assets/TerrainDiffusion/`

The trained weights behind the **V3 terrain generator** (`Code/Procedural/Private/Diffusion/`). These are
not artwork — they are the neural network the generator runs; without them V3 cannot produce terrain and
disables itself.

Source: [xandergos/terrain-diffusion-30m-onnx](https://huggingface.co/xandergos/terrain-diffusion-30m-onnx),
pinned to revision `ad2df557eca5645f588766101cf3bc3682455c3e` — **MIT**.
Upstream project: [terrain-diffusion](https://github.com/xandergos/terrain-diffusion) (SIGGRAPH 2026);
the port is based on its Minecraft mod, [terrain-diffusion-mc](https://github.com/xandergos/terrain-diffusion-mc) (MIT).

| File | Size | Role |
|---|---|---|
| `base_model.onnx` | 2.03 GB | latent stage (flow matching) |
| `decoder_model.onnx` | 224 MB | decoder stage (high-frequency residual) |
| `coarse_model.onnx` | 22 MB | coarse stage (20-step DPM-Solver++) |
| `pipeline_data.json` | 12 KB | WorldClim/ETOPO quantile tables (seed-independent) |
| `world_pipeline_config.json` | 774 B | model constants (30 m/px, latent compression 8, ...) |

These are tracked with **git-lfs** (see `.gitattributes`). A clone without `git lfs pull` leaves ~130-byte
pointer files in their place; `ModelAssets::load` detects that specifically and says so, rather than letting
it surface as an inscrutable protobuf error inside ONNX Runtime.

To re-fetch or update the pinned revision, download the five files from the HuggingFace link above into this
folder. The expected sizes are asserted in `Code/Procedural/Private/Diffusion/ModelAssets.cpp`.

## License summary

- **Poly Haven**: CC0 — "no rights reserved," effectively public domain. No attribution required.
  The only restriction is you can't claim authorship or re-license the originals.
- **ambientCG**: CC0 1.0 Universal — same terms, no attribution required.
- **Terrain Diffusion weights**: MIT — permissive and fine to redistribute, but unlike CC0 it *does* require
  the copyright notice and permission notice to travel with the files. Keep this section (or an equivalent
  notice) if you ship them.

Beyond the MIT notice above, no credits file, in-game acknowledgment, or splash screen is legally necessary
for any of these. This file exists to track provenance (source + license) for future reference.
