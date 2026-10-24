# User reports of speckled / noisy output from Retro Diffusion

Research date: 2026-10-15. Searches covered general web, Reddit (via search engines, because old.reddit.com could not be fetched directly), itch.io comment pages for the Aseprite extension and the website listing, GitHub issues on all four public Retro-Diffusion repos (checked with `gh`), developer devlogs, X/Twitter (via search), Replicate/Scenario pages, and third-party write-ups. Overall result: **very little public evidence**. I found no public report of speckle, salt-and-pepper noise or stray single pixels from RD Pro, rd_pro__topdown, RD Plus, RD Fast, the API or the Replicate models, and none about dense town or city scenes at 192–256 px, input_palette or remove_bg. The only material relevant to noise is: one itch.io "Color Noise" report (local Aseprite extension, Mac M3 Max, likely a backend failure rather than speckle); one community workflow tutorial that calls extension output "noisy" and fixes it with the developer's denoise tools; developer statements that a cleanup filter exists; and developer marketing that names noise as a known failure of generic models.

## Have other users reported speckle, noise or stray pixels from Retro Diffusion? Where, and when?

### Takeaway
Only two items in public, indexed sources come close, and both concern the local Aseprite extension, not the hosted RD Pro/Plus/Fast models or the API. I found nothing on Reddit, GitHub issues, Replicate or X from users about speckled output. The Retro Diffusion Discord, where the developer sends users for support, is not publicly indexed, so most real reports probably live there.

### Cited Findings
- **itch.io, Aseprite extension comments, user "boorch", dated "1 year ago" on the page (about late 2024 to 2025; no exact date shown).** User on a Mac M3 MAX running locally: the first generated image was "okay-ish", but from the second one on "all that's generated is just Color Noise". No other settings given. **Reply from Astropulse:** "I haven't heard of this issue before, would you join the community discord server" (discord.gg/retrodiffusion). No public resolution. This sounds like whole-image noise from a failed local generation, likely an Apple Silicon/MPS backend problem, not fine speckle in otherwise good art. — [itch.io comments page 2](https://astropulse.itch.io/retrodiffusion/comments?before=35)
- **Community tutorial "Landscape Workflow Using Retro Diffusion Smart Tools" by Narik, hosted on astropulse.co (undated).** Of an intermediate generated landscape: "This image does not look "right" yet - it is so noisy and blurry." After color limiting: "The flowers are still noisy, but it would be a hassle to fix them manually." Settings: Palettize (Adaptive, max colors 12, dithering off), then a second Palettize pass with the Lospec500 palette. The remaining noise in the flower area was fixed with the Adopt Pixels script (Edit → FX → Adopt Pixels, Orphan Threshold 2, Denoise Iterations 1) on a lasso-selected region. This is a workflow inside the Aseprite extension, not the web or API models. It is also the only source that links noise to palette reduction and to a dense, detailed region (a flower bed). — [astropulse.co](https://astropulse.co/)
- **itch.io, user "_MHG_", "1 year ago":** "is there a feature to clean up artifacts like doubled pixels, broken lines, jaggies etc.?" **Astropulse:** "It does have a filter for cleaning artifacts, but it is not like doing it by hand." Search snippets of the developer's itch.io profile add that the filter is under "Sprite" → "PixFix Filter" and helps with "removing orphans, cleaning lines, and repairing some colors". I saw that wording only in search-engine summaries, not on a page I fetched. — [itch.io comments page 1](https://astropulse.itch.io/retrodiffusion/comments); [Astropulse itch.io profile](https://itch.io/profile/astropulse)
- **Not relevant (checked and ruled out):** "Nuallán"'s note that "the original images did have some odd artifacting" (about 2 years ago) refers to Bing/ChatGPT images that Nuallán downscaled in Photoshop, not to Retro Diffusion output. Other itch.io quality complaints ("nothing usable", "didnt got anything close") are about prompt adherence or GPU incompatibility, not noise. — [itch.io comments page 2](https://astropulse.itch.io/retrodiffusion/comments?before=35)
- **GitHub:** Retro-Diffusion/api-examples has one issue ever ("MCP server", closed 2026-04-21). pixel-art-fixer, pixel-bench and retro-diffusion-mcp have no issues. No noise reports. — `gh issue list` on [github.com/Retro-Diffusion](https://github.com/Retro-Diffusion/)
- **itch.io listing for the website product (retrodiffusionai):** one comment only ("This is rad."). — [itch.io](https://astropulse.itch.io/retrodiffusionai/comments)
- **Reddit, X, YouTube, Replicate:** several searches found no user threads describing speckle or noise in Retro Diffusion output. Reddit could not be fetched directly (blocked), so a gap remains. — searches only, no source to cite

### Inferences
- The lack of public reports more likely means support happens in Discord than that the problem doesn't exist. Astropulse routinely redirects issue reports there.
- The two noise-related items both come from the local extension, which uses different models from the website and API. The vendor says the extension "cannot" reproduce website results ([Gumroad](https://astropulse.gumroad.com/l/RetroDiffusion)). So neither is direct evidence about RD Pro or rd_pro__topdown.

### Gaps
- No public report found for RD Pro, rd_pro__topdown, RD Plus, RD Fast, the REST API, or Replicate retro-diffusion/rd-plus and rd-fast.
- No exact dates for itch.io comments; the page shows only relative dates.
- Reddit, the Discord archives and the Replicate discussion tabs could not be inspected directly.

## Do the reports tie it to particular styles, sizes, palettes, references, background removal or scene density?

### Takeaway
Hardly at all. The only loose links are to palette reduction (the Narik tutorial: noise stayed in a detailed flower area after limiting to 12 colors and then Lospec500) and, from the developer's own framing, to the cheaper models and to high resolution. I found nothing linking speckle to remove_bg, reference images, the topdown style, or 192–256 px town or city scenes.

### Cited Findings
- Palette reduction did not remove noise in a detailed region: "The flowers are still noisy" after Palettize to 12 adaptive colors with dithering off, then Lospec500. — [astropulse.co](https://astropulse.co/)
- A developer devlog (January update, about 2 years ago) says: "We reduced high resolution artifacts." and "Our team improved the generation quality." This implies artifacts at higher resolutions were a known issue in the extension at that time. — [itch.io devlog 668607](https://astropulse.itch.io/retrodiffusion/devlog/668607/retro-diffusion-january-update-new-composition-editing-menu)
- An "independent benchmark" article published on Astropulse's own X account (RD vs PixelLab, 120 generations) reportedly scores "shape separation" and penalizes scattered pixels and noisy structures. Per search snippets, RD Fast scored lower than RD Pro on structure and readability. The article returned HTTP 402 and could not be read, and because the developer hosts it, its neutrality is unverified. — [X article](https://x.com/RealAstropulse/article/2098426369747091924)
- The founder, quoted on the Runware blog (March 28, 2025), said "the models still have trouble being specifically limited to a set number of colors". The pipeline uses "a proprietary downscaling algorithm that uses clustering techniques" plus "advanced color quantization". This is relevant if speckle comes from clustering or quantization in post-processing. — [Runware blog](https://runware.ai/blog/retro-diffusion-creating-authentic-pixel-art-with-ai-at-scale)
- Scenario's help docs say RD Plus was trained at 256×256 native resolution or lower (snippet only). — [Scenario help](https://help.scenario.com/articles/4202673551-retro-diffusion-models-the-essentials)
- Devlogs describe remove_bg ("backgrounds can now automatically be removed", March 2025 update) and palette input (June 2024). No user complaints about artifacts from either appear. — [QoL and Palettes devlog](https://astropulse.itch.io/retrodiffusion/devlog/903094/retro-diffusion-update-qol-and-palettes); [June 2024 devlog](https://itch.io/devlog/754477/retro-diffusion-update-for-june-palette-control-qol.amp)

### Inferences
- Speculation, not evidence: if speckle appears in dense scenes, a plausible mechanism is the clustering downscaler and palette quantizer assigning lone pixels in high-detail areas. That fits the Narik flower-bed case, but nobody publicly reported it for RD Pro.

### Gaps
- No evidence either way on remove_bg, input_palette, reference images, rd_pro__topdown, or town and city density at 192–256 px.

## What replies, workarounds or fixes did the developers (Astropulse and team) or other users give?

### Takeaway
The developer's public responses are (1) redirecting to Discord, (2) pointing to the extension's PixFix filter, and (3) the separate Adopt Pixels orphan-removal script. The documented community workaround is Palettize with dithering off, followed by Adopt Pixels (Orphan Threshold 2, Denoise Iterations 1) on the noisy region. Retro Diffusion also now offers a free Pixel Art Fixer (grid and cleanup tool).

### Cited Findings
- To the "Color Noise" report, Astropulse said they hadn't heard of the issue and asked the user to join the Discord. — [itch.io](https://astropulse.itch.io/retrodiffusion/comments?before=35)
- Astropulse on the artifact cleanup request: the extension has a filter for cleaning artifacts "but it is not like doing it by hand" (Sprite → PixFix Filter; removes orphans, cleans lines, repairs some colors). — [itch.io](https://astropulse.itch.io/retrodiffusion/comments); [profile](https://itch.io/profile/astropulse)
- Adopt Pixels (Astropulse's Aseprite script) removes orphan pixels. Its threshold is "the number of adjacent pixels of the same color required to keep the subject pixel", and it also has a denoise-iterations setting. — [itch.io Adopt Pixels](https://astropulse.itch.io/adopt-pixels); [Gumroad](https://astropulse.gumroad.com/l/AdoptPixels)
- Community workflow: Palettize (Adaptive, 12 colors, dithering off), then Lospec500, then Adopt Pixels (threshold 2, 1 iteration) on the flowers, then manual detailing. — [astropulse.co](https://astropulse.co/)
- Pixel Art Fixer (open source, standard and neural engines) snaps off-grid "pixel-style" images to a true grid. It is aimed at off-grid cells and blur, not specifically speckle. — [GitHub pixel-art-fixer](https://github.com/Retro-Diffusion/pixel-art-fixer); [retrodiffusion.ai tool page](https://retrodiffusion.ai/tools/pixel-art-fixer/)

### Inferences
- For API users, the closest equivalents are a local orphan-pixel filter (Adopt Pixels logic: drop pixels with fewer than N same-color neighbors) or running Pixel Art Fixer after generation. No developer-endorsed API-side fix for speckle has been published.

### Gaps
- No public developer statement on speckle in RD Pro or API output, and no published fix or changelog entry naming "speckle" or "stray pixels" for the hosted models.
- The Discord support history is not public, and I could not inspect it.
