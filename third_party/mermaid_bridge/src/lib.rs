use std::panic::{AssertUnwindSafe, catch_unwind};
use std::{ptr, slice, str};

use mermaid_rs_renderer::{LayoutConfig, Theme, compute_layout, parse_mermaid_strict, render_svg};

const MAX_SOURCE_BYTES: usize = 16 * 1024;
const MAX_LINES: usize = 256;
const MAX_NODES: usize = 128;
const MAX_EDGES: usize = 256;
const MAX_SVG_BYTES: usize = 2 * 1024 * 1024;

#[repr(C)]
pub struct MermaidResult {
    light: *mut u8,
    light_len: usize,
    dark: *mut u8,
    dark_len: usize,
    width: f32,
    height: f32,
    status: u32,
}

fn bytes(value: String) -> (*mut u8, usize) {
    let mut value = value.into_bytes().into_boxed_slice();
    let result = (value.as_mut_ptr(), value.len());
    std::mem::forget(value);
    result
}

fn failure(message: String) -> MermaidResult {
    let (light, light_len) = bytes(message);
    MermaidResult {
        light,
        light_len,
        dark: ptr::null_mut(),
        dark_len: 0,
        width: 0.0,
        height: 0.0,
        status: 1,
    }
}

fn render(source: &str, font_family: &str) -> Result<MermaidResult, String> {
    if source.len() > MAX_SOURCE_BYTES || source.lines().count() > MAX_LINES {
        return Err("Diagram exceeds the source limit (16 KiB / 256 lines).".into());
    }
    // Require a diagram header: upstream also accepts bare edge-like text, which
    // can accidentally turn invalid prose (e.g. "not-a-diagram") into a graph.
    let first_line = source
        .lines()
        .map(str::trim)
        .find(|line| !line.is_empty() && !line.starts_with("%%"))
        .unwrap_or("");
    let header = first_line
        .split(|c: char| c.is_whitespace() || c == ';')
        .next()
        .unwrap_or("")
        .to_ascii_lowercase();
    if ![
        "flowchart",
        "graph",
        "sequencediagram",
        "classdiagram",
        "statediagram",
        "statediagram-v2",
        "erdiagram",
        "pie",
        "mindmap",
        "journey",
        "timeline",
        "gantt",
        "requirementdiagram",
        "gitgraph",
        "c4",
        "c4context",
        "c4container",
        "c4component",
        "c4dynamic",
        "c4deployment",
        "sankey",
        "sankey-beta",
        "quadrantchart",
        "zenuml",
        "block",
        "block-beta",
        "packet",
        "packet-beta",
        "kanban",
        "architecture",
        "architecture-beta",
        "radar",
        "radar-beta",
        "treemap",
        "treemap-beta",
        "xychart",
        "xychart-beta",
    ]
    .contains(&header.as_str())
    {
        return Err("Unknown or missing Mermaid diagram header.".into());
    }
    // Use strict upstream validation and disable document-controlled configuration.
    if source.contains("%%{init") || source.trim_start().starts_with("---") {
        return Err("Mermaid configuration directives are not supported.".into());
    }
    let parsed = parse_mermaid_strict(source).map_err(|error| error.to_string())?;
    if parsed.init_config.is_some() {
        return Err("Mermaid configuration directives are not supported.".into());
    }
    if parsed.graph.nodes.len() > MAX_NODES || parsed.graph.edges.len() > MAX_EDGES {
        return Err("Diagram exceeds the graph limit (128 nodes / 256 edges).".into());
    }
    let mut light = Theme::modern();
    light.font_family = font_family.to_owned();
    light.background = "none".into();
    let config = LayoutConfig::default();
    let layout = compute_layout(&parsed.graph, &light, &config);
    let size = mermaid_rs_renderer::measure_svg_dimensions(&layout, &config, None);
    if !size.width.is_finite()
        || !size.height.is_finite()
        || size.width <= 0.0
        || size.height <= 0.0
        || size.width > 16384.0
        || size.height > 16384.0
    {
        return Err("Diagram has invalid or excessive dimensions.".into());
    }
    let mut dark = light.clone();
    dark.primary_color = "#283545".into();
    dark.primary_text_color = "#e5eaf0".into();
    dark.text_color = "#e5eaf0".into();
    dark.primary_border_color = "#8dabc9".into();
    dark.line_color = "#b3c6da".into();
    dark.secondary_color = "#253c42".into();
    dark.tertiary_color = "#44372b".into();
    dark.edge_label_background = "#202831".into();
    dark.cluster_background = "#202831".into();
    dark.cluster_border = "#71869b".into();
    dark.sequence_actor_fill = dark.primary_color.clone();
    dark.sequence_actor_border = dark.primary_border_color.clone();
    dark.sequence_actor_line = dark.line_color.clone();
    dark.sequence_note_fill = "#443d25".into();
    dark.sequence_note_border = "#b6a56c".into();
    dark.sequence_activation_fill = "#344657".into();
    dark.sequence_activation_border = dark.primary_border_color.clone();
    dark.pie_title_text_color = dark.text_color.clone();
    dark.pie_legend_text_color = dark.text_color.clone();
    // Both palettes use exactly the same geometry, so theme changes need no relayout.
    let light_svg = render_svg(&layout, &light, &config);
    let dark_svg = render_svg(&layout, &dark, &config);
    if light_svg.len() > MAX_SVG_BYTES || dark_svg.len() > MAX_SVG_BYTES {
        return Err("Rendered diagram exceeds the SVG size limit.".into());
    }
    let (light, light_len) = bytes(light_svg);
    let (dark, dark_len) = bytes(dark_svg);
    Ok(MermaidResult {
        light,
        light_len,
        dark,
        dark_len,
        width: size.width,
        height: size.height,
        status: 0,
    })
}

/// Returned buffers belong to Rust and must be released with mdv_mermaid_free.
///
/// # Safety
/// Non-null inputs must reference readable byte arrays of the specified lengths
/// for the duration of this call. The buffers must not be mutated concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn mdv_mermaid_render(
    source: *const u8,
    source_len: usize,
    font: *const u8,
    font_len: usize,
) -> MermaidResult {
    if source.is_null() || font.is_null() || source_len > MAX_SOURCE_BYTES || font_len > 1024 {
        return failure("Invalid or oversized Mermaid input.".into());
    }
    match catch_unwind(AssertUnwindSafe(|| {
        let source = str::from_utf8(unsafe { slice::from_raw_parts(source, source_len) })
            .map_err(|_| "Mermaid input is not valid UTF-8.".to_string())?;
        let font = str::from_utf8(unsafe { slice::from_raw_parts(font, font_len) })
            .map_err(|_| "Font family is not valid UTF-8.".to_string())?;
        render(source, font)
    })) {
        Ok(Ok(result)) => result,
        Ok(Err(message)) => failure(message),
        Err(_) => failure("The Mermaid renderer failed; showing source instead.".into()),
    }
}

/// Free a buffer returned by mdv_mermaid_render, exactly once with its original length.
///
/// # Safety
/// A non-null pointer must be an outstanding buffer returned by this bridge,
/// with its original length. It must not be accessed after this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn mdv_mermaid_free(data: *mut u8, len: usize) {
    if !data.is_null() {
        drop(unsafe { Box::from_raw(ptr::slice_from_raw_parts_mut(data, len)) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn supported_and_invalid_sources() {
        for source in [
            "flowchart LR; A[Start] --> B[Done]",
            "sequenceDiagram\nAlice->>Bob: Hello",
        ] {
            let result = render(source, "sans-serif").unwrap();
            assert_eq!(result.status, 0);
            assert!(result.width > 0.0 && result.height > 0.0);
            unsafe {
                assert!(
                    str::from_utf8(slice::from_raw_parts(result.light, result.light_len))
                        .unwrap()
                        .contains("<svg")
                );
                mdv_mermaid_free(result.light, result.light_len);
                mdv_mermaid_free(result.dark, result.dark_len);
            }
        }
        assert!(render("not-a-diagram", "sans-serif").is_err());
        assert!(render(&"x".repeat(MAX_SOURCE_BYTES + 1), "sans-serif").is_err());
        assert!(render("%%{init: {}}%%\nflowchart LR; A-->B", "sans-serif").is_err());
    }
}
