# Retro Diffusion official guidance: output noise, palettes, background removal and style choice

Scope: what Retro Diffusion (RD) officially says or implies about how its models make pixel output, and which documented settings might reduce scattered isolated pixels ("speckle") in 256x256 rd_pro__topdown outputs (v1 API, 72-colour input_palette, reference image, remove_bg true, bypass_prompt_expansion true, magenta background prompt).

Sources used (all read 2026-10-15):
- API examples repo README: https://github.com/Retro-Diffusion/api-examples/blob/main/README.md
- llms.txt (agent summary, includes a "Craft guide"): https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt
- EDIT_TOOLS.md: https://github.com/Retro-Diffusion/api-examples/blob/main/EDIT_TOOLS.md
- PIXEL_FIXER.md: https://github.com/Retro-Diffusion/api-examples/blob/main/PIXEL_FIXER.md
- v2 OpenAPI contract: https://github.com/Retro-Diffusion/api-examples/blob/main/contracts/v2/openapi.json
- Hosted API guide with the "Style explorer" (JavaScript page, read in a headless browser): https://www.retrodiffusion.ai/app/guide/api
- Live, unauthenticated edit-tool catalog: https://api.retrodiffusion.ai/v1/edit/tools
- Founder interview (Runware blog, 28 Mar 2025): https://runware.ai/blog/retro-diffusion-creating-authentic-pixel-art-with-ai-at-scale
- Aseprite extension page and devlog (Astropulse, itch.io): https://astropulse.itch.io/retrodiffusion
- Third-party hosts: Replicate blog https://replicate.com/blog/retro-diffusions-pixel-art-models-are-now-on-replicate, Replicate rd-plus readme https://replicate.com/retro-diffusion/rd-plus/readme, Scenario help https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/

## 1. How the models produce pixel output, and how input_palette works

### Takeaway
The founder says the models alone do not reliably produce clean pixel grids or limited colours. RD fixes this with a proprietary post-process: a clustering-based downscale, then colour quantization. input_palette is documented only as "constrains output colors". The docs do not name the quantization method or say whether generation dithers. Both API versions accept `return_pre_palette` (it is in the v2 OpenAPI schema and listed on the hosted guide). The docs give no advice on palette size.

### Cited Findings
- Founder quote: "Even with all of this attention to detail and training, the models still have trouble being specifically limited to a set number of colors, or generating perfect sections of squares" — [Runware blog](https://runware.ai/blog/retro-diffusion-creating-authentic-pixel-art-with-ai-at-scale)
- Post-process described: "they developed a proprietary downscaling algorithm that uses clustering techniques to determine ideal colors for each section, followed by advanced color quantization to reduce the image to a reasonable number of colors without sacrificing detail." — [Runware blog](https://runware.ai/blog/retro-diffusion-creating-authentic-pixel-art-with-ai-at-scale)
- "Getting AI models to generate pixel art is mostly about dataset, data preparation, and post processing. Some AI models can already come pretty close to grid aligned pixels (Flux Dev for example) but getting them to go the final distance is a real challenge." The article also says RD uses a FLUX architecture where "the FLUX model can achieve most styles through prompt engineering alone." — [Runware blog](https://runware.ai/blog/retro-diffusion-creating-authentic-pixel-art-with-ai-at-scale)
- On the pixel-art ideal: "Absence of effects: No partial transparency, glows, blurs, or random noise. Standard diffusion models typically generate the exact opposite: varying pixel sizes, inconsistent outlines, blurry effects, and random noise patterns." — [Runware blog](https://runware.ai/blog/retro-diffusion-creating-authentic-pixel-art-with-ai-at-scale)
- Request-field docs: "`input_palette` | base64 | Constrain output colors to a palette image. Add `return_pre_palette` to also get the un-quantized image." — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md). The hosted guide says the same: "Palette image; output colors are constrained to it. Add return_pre_palette: true to also get the un-quantized image." — [API guide](https://www.retrodiffusion.ai/app/guide/api)
- The v2 OpenAPI inference request schema includes `"return_pre_palette": {"default": false, ... "type": "boolean"}` next to `return_non_bg_removed`, and the response/history schema has a `used_input_palette` flag — [v2 openapi.json](https://github.com/Retro-Diffusion/api-examples/blob/main/contracts/v2/openapi.json)
- On v1: "`https://api.retrodiffusion.ai/v1` remains supported with no retirement plan." The README field table is written for v2 (`POST /v2/inferences`), and the examples can be pointed at v1 with `RD_API_VERSION="v1"`. The repo publishes no full v1 OpenAPI; `contracts/v1/` holds only `error-compatibility.json` — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md), [contracts/v1](https://github.com/Retro-Diffusion/api-examples/tree/main/contracts/v1)
- Palette image guidance: inputs must be "raw base64 with no `data:image/png;base64,` prefix, RGB without transparency, and palette images should stay well under 1 MB." — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)
- For the Low-Poly endpoint, input_palette is described as "A palette image (base64 PNG, JPEG or WEBP, like input_palette on /inferences): the model is built with only its colors (up to 256; larger images are reduced)." — [v2 openapi.json](https://github.com/Retro-Diffusion/api-examples/blob/main/contracts/v2/openapi.json)
- Every rd_pro style (including rd_pro__topdown), every rd_plus style and rd_tile__scene_object list input_palette as a supported field — [API guide Style explorer](https://www.retrodiffusion.ai/app/guide/api)
- Scenario (third-party host) describes RD Plus palettes this way: "You can upload a limited palette image to ensure the model only uses those colors." It also says the models "were trained at resolutions 256×256 pixels and below." — [Scenario help](https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/)
- The Replicate readme says: "Input palette for strict color adherence." and "Retro Diffusion models are designed specifically for grid aligned, style consistent, limited color pixel art generation." — [Replicate rd-plus readme](https://replicate.com/retro-diffusion/rd-plus/readme)

### Inferences
- The founder describes a "clustering per section" downscale followed by quantization. That implies the model generates at a higher internal resolution, and each output pixel is chosen from a block of model pixels. A "pixel" that is a cluster winner can end up as an isolated pixel whenever the underlying model texture is noisy (for example, high-frequency roof-tile or paving texture). Seed-to-seed variation in speckle fits this: some seeds draw smoother texture under the hood. This is an inference. RD does not document the internals.
- With a palette, each pixel is presumably snapped to its nearest palette colour (nearest-colour mapping, inferred). A 72-colour palette with many close dark shades gives more chances for a texture's local value noise to land on distinct dark entries, which would show up as speckle. A smaller palette with fewer near-duplicate darks could merge those values. RD gives no guidance either way, so this would need testing.
- Requesting `return_pre_palette: true` is a direct, documented diagnostic. If the pre-palette image is already speckled, the noise comes from the model or downscale step. If it is clean, the palette mapping causes it. v1 support is not stated explicitly. v1 is described as unchanged and the field predates v2, so it very likely works, but only a test call can confirm it.

### Gaps
- The docs do not state the quantization method for input_palette (nearest colour, perceptual distance, or k-means) or whether it dithers. The edit tools' `dither_mode` defaults to "none", but no doc says the in-generation palette step uses the same code.
- No documented guidance on ideal palette size, or on whether a large palette increases noise.
- No public v1 OpenAPI exists to confirm `return_pre_palette` on v1.

## 2. How remove_bg works and which background colour to use

### Takeaway
RD's own field-tested guidance is to prompt "a flat background color that CONTRASTS the subject", by default "on a plain white background", and set remove_bg true. If the background is left unstated it drifts to dark gray, which "degrades background removal". The method behind remove_bg is not documented. The standalone background_remover tool exposes an alpha `transparency_threshold` (default 190) and `force_solid_pixels` (default true), which suggests a matte/alpha-threshold method rather than a colour key.

### Cited Findings
- llms.txt Craft guide, "field-tested defaults from Retro Diffusion's own production assistant": "Backgrounds: for a standalone asset, state a flat background color that CONTRASTS the subject in the prompt — default "on a plain white background" — and set remove_bg=true. Never write "on a transparent background" into a prompt (transparency is remove_bg's job), and never leave the background unstated: unstated drifts to a drab dark gray that kills contrast and degrades background removal. Scenes are the opposite: describe the real environment and leave remove_bg=false." — [llms.txt](https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt)
- `remove_bg`: "Transparent output. Add `return_non_bg_removed` to also get the original." — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)
- Edit-tool catalog for `background_remover`: description "Remove the background from an uploaded image.", `postPipeline: "background_remover_cleanup"`, fields `force_solid_pixels` (boolean, default "True") and `transparency_threshold` (number 0-255, default "190"), cost $0.01 — [live edit-tools catalog](https://api.retrodiffusion.ai/v1/edit/tools)
- `GET /v2/status` reports `background_removal` as its own service alongside rd_fast/rd_pro/rd_plus — [llms.txt](https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt)
- Aseprite extension devlog: background removal reintroduced with "much higher accuracy and better speed" and it "also works on non-pixel art images!" (no method stated) — [itch.io devlog](https://astropulse.itch.io/retrodiffusion/devlog/631962/retro-diffusion-update-prompt-translator-new-models-background-removal-and-faster-speeds)
- Style descriptions: rd_plus__topdown_asset is "3/4 top down perspective game assets on a simple background" and rd_plus__isometric_asset is "on a neutral background" — [API guide Style explorer](https://www.retrodiffusion.ai/app/guide/api)

### Inferences
- A threshold on alpha (0-255) together with "works on non-pixel art images" points to a learned segmentation/matting model producing a soft alpha, which is then hardened to solid pixels. A chroma key is unlikely. So magenta has no special meaning to the remover. What matters, per the docs, is contrast between subject and background.
- The docs justify white only by contrast and by avoiding the dark-gray drift. They say nothing about speckle. A saturated magenta is not a palette colour in a Roman scheme. If the model renders magenta and the palette lacks it, the background (and any magenta that bleeds into the subject's edges or gaps) gets mapped to whatever palette entry is nearest. That could create odd pixels at edges, but it would not obviously explain dark speckle in roof interiors. `return_non_bg_removed` and `return_pre_palette` together would show what the model actually painted.
- White is the RD default. For pale stone or plaster Roman buildings, white may contrast poorly. The docs' principle ("CONTRASTS the subject") could favour a mid or dark flat colour that is not in the subject, but that is a judgement call the docs don't settle.

### Gaps
- No official statement of the remove_bg algorithm, or of whether it runs before or after palette quantization.
- No doc says whether remove_bg on inference uses the same threshold/force_solid defaults as the background_remover tool.

## 3. Style choice: what the docs say about each style, size ranges, and fit for buildings and towns

### Takeaway
rd_pro__topdown is described as "Pixel art viewed from a 2/3 downwards angle, with simple shapes and shading" (12-256 px). RD Pro is billed as having "the cleanest pixel work". RD Plus top-down styles go up to 384 px and accept img2img but not reference images. rd_tile__scene_object is documented as "Large assets for placing on tileset maps" (64-384 px). No doc recommends a style specifically for buildings or towns.

### Cited Findings
Style explorer descriptions, limits and fields (quoted from the hosted [API guide](https://www.retrodiffusion.ai/app/guide/api)):
- rd_pro__topdown: "Pixel art viewed from a 2/3 downwards angle, with simple shapes and shading." 12-256 px, batch 1-16, $0.18. Fields: reference_images up to 9, input_palette and remove_bg supported. input_image and tile_x/tile_y not supported.
- rd_pro__default: "Clean modern pixel art style model that allows multiple reference images and extremely detailed prompting." 12-256 px, batch 1-16. Same field support as topdown.
- rd_pro__simple: "Simple pixel art with minimal shading or texturing, but strong outlines and shapes." 12-256 px.
- rd_pro__isometric: "Pixel art rotated at a 45 degree angle. Clean lines and shapes." 12-256 px.
- rd_pro__dungeon_map: "Dungeon-crawler style game levels with connected rooms filled with objects and enemies." 64-256 px, batch 1-4.
- rd_pro__pixelate: "Convert input images into pixel art." 16-256 px, input_image required.
- rd_plus__topdown_asset: "3/4 top down perspective game assets on a simple background". 64-384 px, $0.06. input_image with strength 0-1, input_palette, tile_x/y and remove_bg supported. reference_images not supported.
- rd_plus__topdown_map: "Video game map style pixel art with a 3/4 top down perspective". 64-384 px. Same fields as topdown_asset.
- rd_plus__default: "Clean pixel art style with bold colors and outlines". 64-384 px.
- rd_plus__low_res: "High quality, low resolution pixel art assets and backgrounds". 16-128 px.
- rd_plus__classic: "Strongly outlined medium-resolution pixel art with a focus on simple shading and clear design". 32-192 px.
- rd_plus__environment: "One-point perspective scenes with outlines and strong shapes." The explorer lists 64-512 px. The README and llms.txt list 64-384, a minor conflict between sources.
- rd_plus__isometric_asset: "45 degree isometric objects or assets, on a neutral background". 64-384 px.
- rd_plus__topdown_item: "Small 3/4 top down perspective assets with no background". 16-96 px. remove_bg is not supported.
- rd_tile__scene_object: "Large assets for placing on tileset maps". 64-384 px, $0.06.
- rd_tile__tile_object: "Small assets for placing on sections of tiles". 16-96 px.
- rd_fast__low_res: "General low resolution pixel art images". 16-128 px.

Other findings:
- RD Pro: "the cleanest pixel work, the most detailed prompt following, and support for up to 9 reference images to keep a character or art style consistent." — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)
- Sizes by genre: "full scenes/key art 256 (RD Pro's max — for 16:9 use 256x144; pixel art integer-upscales losslessly, so the smaller canvas loses nothing)"; "items/icons/props/critters 32-64" — [llms.txt](https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt)
- Scenario (third party) says RD Tile "does not offer background removal or palette strictness". RD's own explorer lists input_palette and remove_bg for tile styles. — [Scenario help](https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/); [API guide](https://www.retrodiffusion.ai/app/guide/api). The sources conflict here, and RD's explorer is the authoritative one ("Built from the same registry the server enforces").

### Inferences
- rd_pro__simple's "minimal shading or texturing" is the only style description that explicitly promises less texture. If speckle is texture-driven, it is the most promising RD Pro alternative to test against topdown. It is not a top-down style, though, so the perspective would have to come from the prompt and references.
- 256 px is RD Pro's ceiling, and the llms.txt guidance treats 256 as "full scenes" size. A whole Roman town at 256 px is scene-density content, and many small details per pixel plausibly make more single-pixel features. The docs' hint that "pixel art integer-upscales losslessly" implies rendering a building smaller (for example 128 px) and upscaling 2x is an officially sanctioned route to fewer, larger pixel features.
- rd_plus__topdown_asset and rd_tile__scene_object fit "standalone building on a simple background" by description. They lose reference_images (the explorer shows "— reference_images" for RD Plus), which the current pipeline depends on for consistency.

### Gaps
- The docs give no comparison of noise or cleanliness between styles, and no building- or town-specific recommendation.

## 4. Edit tools as a post-step: color_reducer, palette_converter, pixel_correction, k_centroid_downscale

### Takeaway
There is an official, free post-step aimed exactly at speckle: `pixel_correction`, described in the live catalog as "Clean isolated artifacts with the pixel correction filter." color_reducer and palette_converter re-quantize with ordered (Bayer) dithering, which is off by default. They could re-map to a smaller palette without adding dither noise.

### Cited Findings
- Live catalog entry: `pixel_correction`, name "Pixel Correction", description "Clean isolated artifacts with the pixel correction filter.", free, `requires_minimum_balance: false`, no fields other than `input_image` — [live edit-tools catalog](https://api.retrodiffusion.ai/v1/edit/tools); listed as "`pixel_correction` | Free | `input_image`" — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)
- Aseprite extension equivalent (likely related): the developer describes the PixFix filter as helping with "removing orphans, cleaning lines, and repairing some colors", "not like doing it by hand" — [itch.io page](https://astropulse.itch.io/retrodiffusion) (from a developer comment on the page, as summarized by the fetch tool. The exact wording should be rechecked on the page.)
- `color_reducer`: "Reduce an image to fewer colors." Fields `color_count` (2-256, blank = automatic), `dither_mode` (default "none"), `dither_strength` (0-10, default 5). Free, but requires a minimum account balance — [live edit-tools catalog](https://api.retrodiffusion.ai/v1/edit/tools). OpenAPI: "color_reducer: how many colors (omit for automatic)." — [v2 openapi.json](https://github.com/Retro-Diffusion/api-examples/blob/main/contracts/v2/openapi.json)
- `palette_converter`: "Convert an image to a supplied palette image." Fields `input_palette` (required), `dither_mode`, `dither_strength`. Free, minimum balance required — [live edit-tools catalog](https://api.retrodiffusion.ai/v1/edit/tools)
- Dither options: "Supported `dither_mode` values are `none`, `bayer_2x2`, `bayer_4x4`, and `bayer_8x8`. `dither_strength` ranges from `0` to `10` (`0` disables dithering, default `5`), matching the Pixel Fixer's dither scale. Values above `10` are treated as the deprecated `0`-`100` scale and divided by 10." — [EDIT_TOOLS.md](https://github.com/Retro-Diffusion/api-examples/blob/main/EDIT_TOOLS.md). OpenAPI: "ordered dithering between the new colors", enum default "none" — [v2 openapi.json](https://github.com/Retro-Diffusion/api-examples/blob/main/contracts/v2/openapi.json)
- `k_centroid_downscale`: "Downscale an image using local block color centroids." `width`/`height` 1-1024 required, free — [live edit-tools catalog](https://api.retrodiffusion.ai/v1/edit/tools)
- Pixel Fixer: "restores enlarged, softened, AI-rendered, or compressed pixel art to its detected native grid", free, standard (Rust) and neural endpoints, 10 req/min — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md), [PIXEL_FIXER.md](https://github.com/Retro-Diffusion/api-examples/blob/main/PIXEL_FIXER.md)
- Edit endpoints exist on both `/v1/edit/tools/{tool_id}` and `/v2/...`; image fields accept raw base64 or a data URI — [EDIT_TOOLS.md](https://github.com/Retro-Diffusion/api-examples/blob/main/EDIT_TOOLS.md)
- Transparency: inference inputs must be "RGB without transparency" ([README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)). The docs don't say whether pixel_correction preserves alpha on an RGBA input.

### Inferences
- The most direct documented candidate is to run `pixel_correction` on the paletted output, before or after background removal. It is free, it needs no minimum balance, and RD describes it as targeting "isolated artifacts". Its behaviour (neighbour thresholds, whether it keeps colours within the palette, whether it keeps alpha) is undocumented and must be tested.
- `palette_converter` with `dither_mode: none` on the `return_pre_palette` image would let the team re-quantize to a smaller or curated palette, and compare it with RD's in-generation palette step.
- `k_centroid_downscale` (for example 256 to 128) followed by a nearest-neighbour 2x upscale is a documented way to merge single-pixel noise into its block's dominant colour, at the cost of resolution.

### Gaps
- No documented algorithm for pixel_correction (for example, what counts as "isolated").
- No documented alpha handling for color tools or pixel_correction.

## 5. Guidance on detail level, resolution, references, prompt expansion, seeds, and changelog

### Takeaway
RD gives no explicit guidance linking any setting to noise. The relevant documented levers are: describe the subject richly (prompt expansion exists to enrich prompts), reuse a seed to iterate, use smaller canvases with lossless upscale, and use references for consistency. No changelog entry about noise for the hosted RD Pro or Plus models was found.

### Cited Findings
- `bypass_prompt_expansion`: "Skip the automatic LLM prompt enrichment — full manual control over the prompt." — [API guide](https://www.retrodiffusion.ai/app/guide/api)
- "Prompts describe the subject; the style handles pixel-art rendering — rich concrete visual descriptions ... outperform terse ones" — [llms.txt](https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt). README quick-start: "describe the SUBJECT only" / "the style handles the pixel-art look" — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)
- Seeds: "Same seed + settings ≈ same image. Omit for random." and "Reuse the same seed to iterate on a composition; change only the prompt." — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md), [llms.txt](https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt)
- Reference images "guide *style and content* without being redrawn"; "reference_images-based generation RE-IMAGINES rather than converts" — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md), [llms.txt](https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt)
- `negative` "is a placeholder — no current model uses it." So there is no negative-prompt lever against noise — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)
- `upscale_output_factor`: "`1` (default) = native pixel size; higher = nearest-neighbor upscaled PNGs." — [README](https://github.com/Retro-Diffusion/api-examples/blob/main/README.md)
- Custom user styles expose `apply_prompt_fixer` (default true), `llm_instructions`, `user_prompt_template`, `force_palette` and `force_bg_removal` — [llms.txt](https://github.com/Retro-Diffusion/api-examples/blob/main/llms.txt)
- Aseprite extension advice: "You can create smaller pixel art and upscale it." — [itch.io page](https://astropulse.itch.io/retrodiffusion). Extension caveat: the website and extension "use different models" — [itch.io Lite page](https://astropulse.itch.io/retrodiffusionlite)
- Extension devlog notes mention "reduced high resolution artifacts" (January update) and Palettize "best palette" accuracy improvements. These concern the local Aseprite extension, not the hosted API models — [itch.io devlog](https://astropulse.itch.io/retrodiffusion/devlog)
- Scenario (third party): "Enlarging outputs beyond their native size may introduce visible pixel duplication or color bleed."; tips "Match the intended resolution." and "Use palettes for color control." — [Scenario help](https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/)

### Inferences
- With `bypass_prompt_expansion: true`, the prompt as written is what conditions the model. If the prompt asks for rich surface detail (roof tiles, paving stones, textures), the model will likely render high-frequency texture, which the downscale or palette step turns into single pixels. Prompting for "flat", "simple shading" or "clean large shapes" (the language RD uses for rd_pro__simple and rd_pro__topdown) may reduce speckle. This is untested inference.
- The wide seed-to-seed variance is consistent with RD's "same seed ≈ same image" model. Seed screening (generate a batch, score speckle automatically, keep the cleanest) works within the documented behaviour. Batches of up to 16 are allowed for rd_pro__topdown.
- References "re-imagine" rather than copy. A noisy reference image could propagate texture density, but no doc says so.

### Gaps
- No official statement links seeds, references, prompt expansion or palette size to noise.
- No public changelog for the hosted API models that mentions noise or artifact fixes. The RD Discord, which RD says is its main support channel, was not searchable.
- No Astropulse posts on X describing the hosted palette step were found (X is not indexed by these tools). The only primary developer statement on mechanism is the founder quote in the Runware interview.
