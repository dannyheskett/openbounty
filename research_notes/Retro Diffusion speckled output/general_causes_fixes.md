# General causes of, and fixes for, speckled or noisy output in AI pixel-art generation

Scope note: these notes cover the general pipeline (model, then grid downscale, palette quantization, dithering, background removal) and compare tools. About 15 tool calls were made. Primary sources were product READMEs, tool docs and practitioner write-ups. No peer-reviewed study measures speckle in AI pixel art specifically. Vendor claims are self-reported.

## Q1: What causes isolated pixels (quantization error diffusion, nearest-colour mapping of anti-aliased detail, downscale method, too much detail, background removal edges)?

### Takeaway
The root cause is that diffusion models produce continuous-tone images with soft edges, anti-aliasing and off-grid "pseudo-pixels". Every later step that forces this onto a hard grid and a fixed palette (point-sampled downscaling, nearest-colour mapping, error-diffusion dithering, alpha thresholding) turns that sub-pixel ambiguity into single-pixel colour decisions, which show up as speckle. The sources document the mechanisms qualitatively. None ranks how much each cause contributes.

### Cited Findings
- **Model level:** diffusion models "work in a continuous latent space, not on a discrete pixel grid", so they have no concept of hard edges or fixed palettes. AI output has a grid that is "all over the place", and its pixels "bleed over, blur at the edges, or just float slightly off". Palette extraction shows a large number of colours. Limiting the palette is described as reducing "soft gradients, anti-aliasing, and random color speckles." — [DEV: How to tame your AI pixel art (jenissimo)](https://dev.to/jenissimo/how-to-tame-your-ai-pixel-art-3pk5)
- **Downscale method:** a naive nearest-neighbour pass leaves edges muddy and colours bleeding, because the AI's "pixels" don't line up with any grid. The author's fix was block-wise dominant-colour voting. — [DEV (jenissimo)](https://dev.to/jenissimo/how-to-tame-your-ai-pixel-art-3pk5)
- Standard scaling often fails on AI pixel art because of inconsistent pixel sizes and non-square grids. The perfect-pixel tool therefore detects the grid (FFT plus Sobel edge alignment) before sampling. — [perfect-pixel GitHub (theamusing)](https://github.com/theamusing/perfectpixel)
- **K-Centroid's purpose:** Astropulse (the Retro Diffusion developer) describes K-Centroid as a downscaler designed to "keep images close to the original, while also reducing noise and blur". It uses K-Means to find the most common colour for each output tile, which implies that conventional methods (nearest or bilinear) add noise or blur. — [K-Centroid on itch.io](https://astropulse.itch.io/k-centroid); [K-Centroid-Aseprite GitHub](https://github.com/Astropulse/K-Centroid-Aseprite). The README shows visual comparisons against bilinear and nearest neighbour but gives no metrics.
- **Upscaling or off-native size:** for Retro Diffusion models, scaling beyond the native size "may introduce visible pixel duplication or color bleed". Unsupported sizes "may result in errors or unexpected results." RD Plus was trained at native resolutions of 256×256 or below. — [Scenario help: Retro Diffusion Models – The Essentials](https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/)
- **Dithering, error diffusion:** Floyd-Steinberg produces a characteristic "directional and latticed" artifact pattern [(Lin & Wang, SciTePress 2011)](https://www.scitepress.org/Papers/2011/33266). The dithering literature calls these "undesired texture patterns". Dithering is normally applied as the final colour-reduction step because later resampling disturbs the pattern. — [Albumentations blog on dithering](https://albumentations.ai/blog/2025/02-the-art-and-science-of-dithering/); [Transloadit glossary](https://transloadit.com/glossary/floyd-steinberg-dithering/)
- The ComfyUI PixelArt Detector exposes dithering as an optional toggle "for a more retro look", so dithering is opt-in, not required. — [ComfyUI-PixelArt-Detector](https://github.com/dimtoneff/ComfyUI-PixelArt-Detector)
- **Too much detail for the resolution (craft view):** "Depicting every single brick would appear noisy." A good texture "may only need a few simple clusters." "If you have the resolution to do so, you're probably not making pixel art." — [Slynyrd Pixelblog 2: Texture](https://slynyrd.com/blog/2018/2/15/pixelblog-2-texture)
- **Background removal edges:** cleanup tools treat alpha as a separate step. unfake offers "alpha binarization" with a threshold (for example `--alpha-threshold 200`) and edge-flood-fill background removal with a colour tolerance (default 10). — [unfake on PyPI](https://pypi.org/project/unfake/). fal.ai Image2Pixel exposes `background_tolerance` and `background_mode` alongside `cleanup_morph` and `cleanup_jaggy`. — [fal.ai Image2Pixel llms.txt](https://fal.ai/models/fal-ai/image2pixel/llms.txt)
- PixelLab's remove-background tool has a "Simple" mode and a "Complex" mode, which takes a subject description. — [PixelLab docs: remove background](https://www.pixellab.ai/docs/tools/remove-background)

### Inferences
- The likely causal chain: the model renders sub-pixel detail and anti-aliased edges. Point-sampling (nearest neighbour) on a misaligned grid picks an arbitrary sample from a mixed block. Nearest-colour mapping then sends each intermediate shade to whichever palette entry is closest, and neighbouring pixels near a decision boundary flip between two palette colours. The result is salt-and-pepper speckle. Error diffusion, if enabled, adds more isolated pixels by design.
- Background removal with a soft matte followed by a hard alpha threshold probably leaves one-pixel fringe and stray opaque pixels on the boundary. This is inferred from tools offering alpha binarization and tolerance controls. No source shows it directly.
- The listed causes are not exclusive. "Too much detail for the resolution" is the upstream cause, and the downscale and quantization steps decide whether that detail turns into speckle.

### Gaps
- I found no measurement that ranks the causes (for example, the share of orphans from dithering versus from quantization versus from the downscale method).
- I found no technical documentation of how Retro Diffusion's hosted models enforce the palette internally. The developer says palette and lighting settings are applied in generation, "not as post processing effects" ([Scenario help](https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/)), but the mechanism is undocumented.
- I could not read Astropulse's pixeldetector README for algorithm details. It only says the tool restores downscaled or JPG-corrupted pixel art and predicts a palette, with `-m` max colours defaulting to 128. — [pixeldetector GitHub](https://github.com/Astropulse/pixeldetector)

## Q2: Does constraining to a fixed palette (around 72 colours) increase speckle compared with the model's own colours? Does a smaller palette help or hurt?

### Takeaway
No source tests this directly. Practitioner tools treat reducing the palette before downscaling as a noise reducer, because collapsing similar shades gives each block a clear dominant colour. Forcing an external fixed palette onto an image whose colours do not match it is a different operation, and in principle it can increase boundary flicker and orphans. Whether 72 colours helps or hurts depends on how well that palette fits the image's colours, and that is untested.

### Cited Findings
- Quantizing to a limited palette (Wu algorithm) before the dominant-colour downscale "collapse[s] similar colors" so each block has a clearer winner. A second quantization pass after downscaling removes new colours created by mean-colour blending. — [DEV (jenissimo)](https://dev.to/jenissimo/how-to-tame-your-ai-pixel-art-3pk5)
- In the ComfyUI PixelArt Detector, resize order depends on input size. Images larger than the target are quantized first and downscaled after. Images smaller than the target are upscaled before quantizing. — [ComfyUI-PixelArt-Detector](https://github.com/dimtoneff/ComfyUI-PixelArt-Detector)
- Phaser's "Tidy Pixel Art" tool "snaps colors to a tighter palette", which it presents as removing colour noise. — [Phaser Tidy Pixel Art](https://phaser.io/tools/tidy-pixel-art)
- The same detector offers `cleanup_colors` / `cleanup_pixels_threshold`, which removes rarely used colours. "Good values: 0.01-0.05." "LOWER VALUE = MORE COLORS." The README recommends combining it with `reduce_colors_before_palette_swap`, meaning reduce to the image's own dominant colours first and then swap to the target palette. It recommends PIL's MAXCOVERAGE quantize method for pixel art and calls FASTOCTREE "fast but poor quality". — [ComfyUI-PixelArt-Detector](https://github.com/dimtoneff/ComfyUI-PixelArt-Detector)
- Retro Diffusion's devlog says the new Palettize "Automatic" algorithm is "more accurate and uses less colors on average", and that palette swapping is "much more accurate" using small AI models trained in real time. — [Retro Diffusion devlog: QoL and Palettes](https://astropulse.itch.io/retrodiffusion/devlog/903094/retro-diffusion-update-qol-and-palettes)
- RD Plus accepts an uploaded palette image so the model "only uses those colors". RD Tile has no palette strictness. — [Scenario help](https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/)
- Craft guidance: too many shades clutter a small canvas. — [Phaser/guide summaries via search; general pixel art guidance](https://phaser.io/tools/tidy-pixel-art) (weak source; treat as general advice).

### Inferences
- Speculation, mechanistically reasoned: a fixed palette of about 72 colours has many near-neighbour entries. Pixels whose true colour falls between two entries flip between them on small variations, which produces speckle within flat regions. A smaller palette fitted to the image (k-means or Wu on the image itself) has more widely spaced entries, and that should reduce flicker. A smaller fixed palette that fits the image poorly can instead cause banding or posterization, and with dithering on, more speckle.
- The pattern practitioners use is reduce, then swap. First cluster to the image's own dominant colours (say 8 to 32), merge rare colours, and only then map to the target palette. This avoids per-pixel nearest-colour mapping straight from noisy continuous colour.
- Using the in-model palette constraint (RD Plus palette input) instead of post-hoc mapping is claimed by the vendor to be integrated, but no evidence was found that it yields less speckle.

### Gaps
- I found no experiment comparing orphan counts for a fixed 72-colour palette against the model's native colours or a smaller palette. This would need a local test (generate N images, quantize under each condition, count orphans).

## Q3: Which remedies work (fewer and larger subjects, lower resolution, different downscale method, majority or median filters, prompting, seed selection by noise score, batch-and-pick)?

### Takeaway
The best-documented remedies are (1) detecting and aligning the grid, then downscaling by block majority or mode (dominant colour or K-Centroid) instead of nearest neighbour, (2) reducing the palette before downscaling and merging rare colours, and (3) a neighbourhood majority filter that recolours isolated pixels. Astropulse's Adopt Pixels, ClusterSweep, unfake's morph and jaggy cleanup, and fal.ai's cleanup_morph all do this last step. Generating at native or lower resolution with simpler subjects is supported by craft guidance and vendor advice. Prompting for clean or flat output is advised by a tool README but not measured. Seed scoring by noise is not documented anywhere I found.

### Cited Findings
- **Majority or mode downscale:** unfake's default `dominant` method takes the most frequent colour per scale×scale block, falls back to the mean if no colour dominates, and "Preserves original palette colors". Other options are `median` (suggested for photos), `mode`, and `content-adaptive` (Kopf & Lischinski 2011, higher quality but slower). Scale detection uses colour-run GCD, with Sobel edge detection as a fallback for anti-aliased images. — [unfake on PyPI](https://pypi.org/project/unfake/)
- Dominant-colour voting rule: a colour wins a block if it beats the runner-up by more than 5%. Otherwise the block gets a mean-RGB blend. — [DEV (jenissimo)](https://dev.to/jenissimo/how-to-tame-your-ai-pixel-art-3pk5)
- perfect-pixel offers `center`, `median` and `majority` sampling after grid detection. It recommends input images of 512–1024 px and `refine_intensity` in [0, 0.5]. — [perfect-pixel GitHub](https://github.com/theamusing/perfectpixel)
- K-Centroid uses per-tile K-Means for the dominant colour and is available as an Aseprite extension and as a Retro Diffusion API edit tool (free to $0.01). The same API also lists "background remover, palette converter, color reducer, pixel correction". — [K-Centroid itch.io](https://astropulse.itch.io/k-centroid); [Retro Diffusion MCP listing](https://mcpservers.org/servers/retro-diffusion/retro-diffusion-mcp)
- **Orphan-pixel majority filter (Adopt Pixels, by Astropulse):** "a denoise algorithm that removes 'orphan pixels' in a palette and style friendly way". It checks neighbours, and "if there aren't enough of the same color pixel it changes the color to the most common of the surrounding pixels." "Threshold is the number of adjacent pixels of the same color required to keep the subject pixel". Iterations repeat the pass, and the mushroom-house example needed threshold 3 with 6 iterations. Typical denoising "tends to completely destroy sprites" at pixel-art scale. — [Adopt Pixels (itch.io)](https://astropulse.itch.io/adopt-pixels)
- ClusterSweep: a one-click despeckle that finds isolated 1×1 pixels and merges them into their dominant neighbour. — [ClusterSweep (itch.io)](https://gregorigin.itch.io/clustersweep)
- unfake cleanup: "morph" ("Morphological operations to remove noise") and "jaggy" ("Removes isolated diagonal pixels"). — [unfake](https://pypi.org/project/unfake/). The jenissimo pipeline applies morphological cleanup before downscaling to remove stray pixels. — [DEV](https://dev.to/jenissimo/how-to-tame-your-ai-pixel-art-3pk5)
- **Stable Diffusion LoRAs:** Pixel Art XL advises generating large and then "Downscale 8 times to get pixel perfect images" with nearest neighbour, using the negative prompt "3d render, realistic" and a fixed VAE (0.9 or fp16 fix) to avoid artifacts. — [nerijs/pixel-art-xl on Hugging Face](https://huggingface.co/nerijs/pixel-art-xl)
- A user of a node-based tool found that one large nearest-neighbour reduction merged colours and lost detail, and preferred several smaller steps. This is anecdotal. — [itch.io post](https://itch.io/post/13916320)
- **Prompting:** perfect-pixel's README gives prompt guidance for the input image: few active pixels, solid colours, no anti-aliasing. — [perfect-pixel GitHub](https://github.com/theamusing/perfectpixel)
- **Native resolution:** generate at or near the model's native or supported size, and avoid enlarging. Iterate on prompt, style, palette or dimensions if the result is wrong. — [Scenario help](https://help.scenario.com/en/articles/retro-diffusion-models-the-essentials/)
- **Simpler subjects and texture:** reduce detail to simple abstract clusters. Orphans "tend to distract the eye" unless they relate to a larger texture. — [Slynyrd Pixelblog 2](https://slynyrd.com/blog/2018/2/15/pixelblog-2-texture)
- Lesson material lists orphan pixels, jaggies and doubles as common line mistakes, with the fix of deleting the lone pixel or reattaching it to a shape. — [Wayline Pixel Art for Games, lesson 2](https://www.wayline.io/learn/pixel-art/1)

### Inferences
- Probable order of effectiveness, based on how many independent tools converge on it (not measured): grid alignment plus mode/majority downscaling, then palette reduction before downscaling with rare-colour merging, then a neighbourhood majority filter (threshold about 2–3 same-colour neighbours with a few iterations), then disabling dithering, then cleaning the alpha edge (binarize, then remove opaque islands on the boundary).
- Fewer, larger subjects per canvas and lower output resolution should reduce speckle because each object gets more pixels per feature. This follows from the craft guidance on detail and resolution, but I found no AI-specific test.
- The Adopt Pixels threshold rule (count of same-colour 8-neighbours) also works directly as a noise score for ranking seeds or batches. That use is my proposal, not documented practice.
- Caveat: majority filters also remove intentional single-pixel highlights (eyes, specular glints, stars). Practitioners note exceptions for texture ([Slynyrd](https://slynyrd.com/blog/2018/2/15/pixelblog-2-texture)), so a mask or a low threshold may be needed.

### Gaps
- I found no documentation from Retro Diffusion, PixelLab or Scenario recommending "clean", "flat shading" or "simple shapes" prompts specifically to reduce speckle, and no evidence on whether such prompts work.
- I found no published practice of automatic seed selection by a noise score. Batch-and-pick is implied by "iterate" advice but not formalized.
- I found no PixelLab-specific guidance on noise beyond its remove-background docs. Its full documentation was not retrieved.
- Retro Diffusion's "pixel correction" edit tool is listed, but its behaviour is undocumented in the sources found.

## Q4: Is there a standard metric for orphan or isolated pixels in pixel art?

### Takeaway
No. There is a widely shared community definition (a single pixel not connected to a same-colour cluster), and tools implement threshold rules based on neighbour counts, but I found no named or standardized metric.

### Cited Findings
- Definition: "Orphan pixels are single pixel units not connected to a cluster of the same color." — [Slynyrd Pixelblog 2](https://slynyrd.com/blog/2018/2/15/pixelblog-2-texture)
- Operational rule in a tool: keep a pixel only if at least *threshold* adjacent pixels share its colour; otherwise recolour it to the neighbourhood mode. — [Adopt Pixels](https://astropulse.itch.io/adopt-pixels)
- ClusterSweep targets "isolated 1x1 pixels", meaning zero same-colour neighbours. — [ClusterSweep](https://gregorigin.itch.io/clustersweep)

### Inferences
- A defensible ad-hoc metric: orphan rate, defined as the fraction of opaque pixels with 0 same-colour neighbours (4- or 8-connectivity; state which). Optionally report a "weak pixel" rate (at most 1 same-colour neighbour) and the number of connected components of size 1–2 per 1,000 pixels. Exclude transparent pixels and decide whether outline pixels count. This would match the Adopt Pixels and ClusterSweep definitions, so results would carry over to their cleanup.

### Gaps
- I found no academic or community standard (Lospec, Pixel Joint, r/PixelArt) defining a numeric orphan or speckle metric. Pixel Joint and Reddit threads were not reached directly in the searches.
