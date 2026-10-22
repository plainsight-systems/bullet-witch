# Contemporary graphics pipeline: Softimage interview

Reviewed October 22, 2026. [Engine history](cavia-engine-history.md).

Primary source: Yoshiyuki Watanabe's Softimage customer interview with Cavia,
[archived October 14, 2007](https://web.archive.org/web/20071014174745/http://softimage.jp:80/user_case/bulletwitch/index.html).
The page discusses the released 2006 game; its exact publication date is unstated.

It identifies 浅見俊則 as responsible for XSI plugins and the Xbox 360 rendering
engine, and 小池利幸 as chief programmer working on realtime shaders.
Cavia used the XSI SDK to send data directly to Xbox 360 development hardware
without an intermediate format. Their plugin environment supported C++, JScript
and VBScript.

The character workflow combined XSI base models, ZBrush detailing, polygon
reduction and Ultimapper normal/AO baking. Reference models linked animation work
to updated characters. Gator transferred skin weights, UVs and vertex colors to
new costumes. The interview describes HDR, bloom, realtime shadows, normal maps,
and over 1,000 ambient lights in a stage. Large photon-map generation used 64-bit XSI.

This supplies concrete authoring/rendering leads. It does not expose the proprietary
exporter or establish the exact Steam rendering implementation.

Retrieval: Wayback availability API resolved the snapshot; Python urllib read its
`id_` HTML, decoded Japanese text and removed markup for inspection. Web-tool
retrieval failed, but direct retrieval succeeded. No full article copy retained.
