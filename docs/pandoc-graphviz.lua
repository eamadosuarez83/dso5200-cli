-- pandoc-graphviz.lua
--
-- Pandoc Lua filter: converts fenced code blocks tagged ```dot or ```graphviz
-- into rendered images via Graphviz's `dot` command, so diagrams written in
-- DOT syntax show up as actual figures in any pandoc output format (PDF via
-- LaTeX, HTML, docx, etc.) instead of being left as plain code text.
--
-- Requires the `dot` binary (package `graphviz`) to be on PATH.
--
-- Usage:
--   pandoc GUIA_ESTUDIO.md --lua-filter=pandoc-graphviz.lua -o guia.pdf
--
-- Output format is chosen per pandoc's target: SVG for HTML-like outputs
-- (crisp at any zoom), PNG otherwise (safest for LaTeX/PDF, docx, etc.).

local image_counter = 0

local function render_dot(code, filetype)
  local dot_path = os.tmpname()
  local out_path = os.tmpname() .. "." .. filetype

  local dot_file = io.open(dot_path, "w")
  dot_file:write(code)
  dot_file:close()

  local ok = os.execute(string.format('dot -T%s "%s" -o "%s"', filetype, dot_path, out_path))
  os.remove(dot_path)

  if not ok then
    io.stderr:write("pandoc-graphviz.lua: 'dot' failed to render a diagram (is graphviz installed?)\n")
    return nil
  end
  return out_path
end

function CodeBlock(block)
  if not (block.classes[1] == "dot" or block.classes[1] == "graphviz") then
    return nil
  end

  local filetype = "png"
  if FORMAT:match("html") then
    filetype = "svg"
  end

  local image_path = render_dot(block.text, filetype)
  if not image_path then
    return nil -- leave the original code block in place so the failure is visible
  end

  image_counter = image_counter + 1
  return pandoc.Para({
    pandoc.Image({}, image_path, "", {id = "diagrama-" .. image_counter}),
  })
end
