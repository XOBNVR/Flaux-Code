/* Flaux Studio's block editor (Blocks mode), built on Blockly.
 *
 * - defines a block for every Flaux instruction (built-in functions come
 *   from blocks_table.json, shared with blocks.py)
 * - builds the toolbox (the block menus on the left)
 * - turns blocks into Flaux code, remembering which block made each line, so
 *   problems can be shown on the right block
 *
 * Code becomes blocks on the server (blocks.py), using the same block names.
 */
"use strict";

window.FlauxBlocks = (() => {
  let ws = null, info = null, loading = false, onChange = () => {};
  const PICTURE_NOTE = "Pictures must be in the same folder as your program.";

  // ------------------------------------------------------------ blocks
  function defineBlocks() {
    const cat = info.table.categories;
    const stmt = { previousStatement: null, nextStatement: null };
    const v = (name) => ({ type: "input_value", name });
    const s = (name) => ({ type: "input_statement", name });
    const dummy = { type: "input_dummy" };
    const variable = (name) => ({ type: "field_variable", name: "VAR", variable: name });
    const defs = [
      { type: "flx_say", message0: "say %1", args0: [v("VALUE")], ...stmt, colour: cat["Say & Ask"],
        tooltip: "Show something on the screen." },
      { type: "flx_say_blank", message0: "say an empty line", ...stmt, colour: cat["Say & Ask"],
        tooltip: "Show an empty line." },
      { type: "flx_say_2", message0: "say %1 %2", args0: [v("VALUE0"), v("VALUE1")], inputsInline: true, ...stmt,
        colour: cat["Say & Ask"], tooltip: "Show two things on one line, with a space between." },
      { type: "flx_say_3", message0: "say %1 %2 %3", args0: [v("VALUE0"), v("VALUE1"), v("VALUE2")], inputsInline: true,
        ...stmt, colour: cat["Say & Ask"], tooltip: "Show three things on one line." },
      { type: "flx_say_4", message0: "say %1 %2 %3 %4", args0: [v("VALUE0"), v("VALUE1"), v("VALUE2"), v("VALUE3")],
        inputsInline: true, ...stmt, colour: cat["Say & Ask"], tooltip: "Show four things on one line." },
      { type: "flx_add_item", message0: "add %1 to item %2 of %3", args0: [v("VALUE"), v("INDEX"), v("LIST")],
        inputsInline: true, ...stmt, colour: cat.Lists, tooltip: "Add to one item of a list." },
      { type: "flx_subtract_item", message0: "subtract %1 from item %2 of %3", args0: [v("VALUE"), v("INDEX"), v("LIST")],
        inputsInline: true, ...stmt, colour: cat.Lists, tooltip: "Take away from one item of a list." },
      { type: "flx_add", message0: "add %1 to %2", args0: [v("VALUE"), variable("score")], inputsInline: true,
        ...stmt, colour: cat.Variables, tooltip: "Add to a number, join onto text, or put an item in a list." },
      { type: "flx_subtract", message0: "subtract %1 from %2", args0: [v("VALUE"), variable("score")],
        inputsInline: true, ...stmt, colour: cat.Variables, tooltip: "Take away from a number." },
      { type: "flx_set_item", message0: "set item %1 of %2 to %3", args0: [v("INDEX"), v("LIST"), v("VALUE")],
        inputsInline: true, ...stmt, colour: cat.Lists, tooltip: "Change one item of a list. Item 1 is the first." },
      { type: "flx_repeat", message0: "repeat %1 times %2 %3", args0: [v("TIMES"), dummy, s("DO")],
        inputsInline: true, ...stmt, colour: cat.Control, tooltip: "Do the blocks inside this many times." },
      { type: "flx_repeat_range", message0: "repeat with %1 from %2 to %3 %4 %5",
        args0: [variable("i"), v("FROM"), v("TO"), dummy, s("DO")], inputsInline: true, ...stmt,
        colour: cat.Control, tooltip: "Count from one number to another." },
      { type: "flx_repeat_range_by", message0: "repeat with %1 from %2 to %3 by %4 %5 %6",
        args0: [variable("i"), v("FROM"), v("TO"), v("BY"), dummy, s("DO")], inputsInline: true, ...stmt,
        colour: cat.Control, tooltip: "Count in steps. Use a minus step to count down." },
      { type: "flx_repeat_each", message0: "repeat with %1 in %2 %3 %4",
        args0: [variable("item"), v("LIST"), dummy, s("DO")], inputsInline: true, ...stmt, colour: cat.Control,
        tooltip: "Do the blocks inside once for each item of a list, or each letter of text." },
      { type: "flx_repeat_while", message0: "repeat %1 %2 %3 %4",
        args0: [{ type: "field_dropdown", name: "MODE", options: [["while", "WHILE"], ["until", "UNTIL"]] },
                v("COND"), dummy, s("DO")], inputsInline: true, ...stmt, colour: cat.Control,
        tooltip: "Keep going while (or until) the check is true." },
      { type: "flx_forever", message0: "repeat forever %1 %2", args0: [dummy, s("DO")], ...stmt,
        colour: cat.Control, tooltip: "Keep going for ever. Games put their main loop in here." },
      { type: "flx_stop", message0: "stop repeating", previousStatement: null, colour: cat.Control,
        tooltip: "Leave the repeat this block is in." },
      { type: "flx_skip", message0: "skip to the next time round", previousStatement: null, colour: cat.Control,
        tooltip: "Jump straight to the next time round the repeat." },
      { type: "flx_return", message0: "give back %1", args0: [v("VALUE")], previousStatement: null,
        colour: cat.Functions, tooltip: "Finish the function and give back this value." },
      { type: "flx_return_nothing", message0: "leave the function", previousStatement: null,
        colour: cat.Functions, tooltip: "Finish the function now." },
      { type: "flx_note", message0: "# %1", args0: [{ type: "field_input", name: "TEXT", text: "a note to myself" }],
        ...stmt, colour: cat.Notes, tooltip: "A note for people. Flaux ignores it." },
      { type: "flx_raw_stmt", message0: "code %1", args0: [{ type: "field_input", name: "CODE", text: 'say "hi"' }],
        ...stmt, colour: cat.Notes, tooltip: "Any line of Flaux code." },
      { type: "flx_raw_expr", message0: "code %1", args0: [{ type: "field_input", name: "CODE", text: "1 + 2" }],
        output: null, colour: cat.Notes, tooltip: "Any Flaux value or sum, written as code." },
      { type: "flx_number", message0: "%1", args0: [{ type: "field_input", name: "NUM", text: "0" }],
        output: null, colour: cat.Maths, extensions: ["flx_number_check"], tooltip: "A number." },
      { type: "flx_arith", message0: "%1 %2 %3", inputsInline: true, output: null, colour: cat.Maths,
        args0: [v("A"), { type: "field_dropdown", name: "OP", options: [["+", "ADD"], ["−", "SUB"],
          ["×", "MUL"], ["÷", "DIV"], ["÷ whole", "IDIV"], ["mod", "MOD"], ["^", "POW"]] }, v("B")],
        tooltip: "Maths. ÷ gives decimals, ÷ whole drops them, mod is the remainder, ^ is power." },
      { type: "flx_compare", message0: "%1 %2 %3", inputsInline: true, output: null, colour: cat.Checks,
        args0: [v("A"), { type: "field_dropdown", name: "OP", options: [["=", "EQ"], ["≠", "NEQ"],
          ["<", "LT"], [">", "GT"], ["≤", "LTE"], ["≥", "GTE"]] }, v("B")],
        tooltip: "Compare two things. Gives true or false." },
      { type: "flx_logic", message0: "%1 %2 %3", inputsInline: true, output: null, colour: cat.Checks,
        args0: [v("A"), { type: "field_dropdown", name: "OP", options: [["and", "AND"], ["or", "OR"]] }, v("B")],
        tooltip: "and: both are true. or: at least one is true." },
      { type: "flx_not", message0: "not %1", args0: [v("VALUE")], output: null, colour: cat.Checks,
        tooltip: "Flips true and false." },
      { type: "flx_negate", message0: "− %1", args0: [v("VALUE")], output: null, colour: cat.Maths,
        tooltip: "Minus this number." },
      { type: "flx_index", message0: "item %1 of %2", args0: [v("INDEX"), v("LIST")], inputsInline: true,
        output: null, colour: cat.Lists, tooltip: "One item of a list (or letter of text). Item 1 is the first." },
      { type: "flx_length", message0: "length of %1", args0: [v("VALUE")], output: null, colour: cat.Lists,
        tooltip: "How many items a list has, or letters text has." },
      { type: "flx_copies", message0: "%1 copies of %2", args0: [v("COUNT"), v("VALUE")], inputsInline: true,
        output: null, colour: cat.Lists, tooltip: "A list with the same thing many times." },
      { type: "flx_colour", message0: "%1", output: null, colour: cat.Drawing, tooltip: "A colour.",
        args0: [{ type: "field_dropdown", name: "COLOUR",
          options: info.colors.map((c) => [c.replace(/^(dark|light)/, "$1 "), c]) }] },
      { type: "flx_key", message0: "%1", output: null, colour: cat["Keys & Mouse"], tooltip: "A key on the keyboard.",
        args0: [{ type: "field_dropdown", name: "KEY", options: info.table.keys.map((k) => [k, k]) }] },
    ];
    for (const f of info.table.functions) {
      const def = { type: blockType(f), message0: f.label, colour: info.table.categories[f.category],
        inputsInline: true, tooltip: `${f.name}(...)` + (f.args.includes("pic") ? "  " + PICTURE_NOTE : "") };
      if (f.args.length) def.args0 = f.args.map((_, i) => v("ARG" + i));
      if (f.kind === "statement") Object.assign(def, stmt);
      else def.output = null;
      defs.push(def);
    }
    if (!Blockly.Extensions.isRegistered || !Blockly.Extensions.isRegistered("flx_number_check")) {
      Blockly.Extensions.register("flx_number_check", function () {
        this.getField("NUM").setValidator((text) => {
          const t = String(text).trim();
          return /^-?\d[\d_]*(\.\d+)?(e[+-]?\d+)?$/i.test(t) ? t : null;
        });
      });
    }
    Blockly.common.defineBlocksWithJsonArray(defs);
  }

  function blockType(f) { return `fn_${f.name}_${f.args.length}`; }

  // ----------------------------------------------------------- toolbox
  function toolbox() {
    const cats = info.table.categories;
    const num = (n) => ({ shadow: { type: "flx_number", fields: { NUM: String(n) } } });
    const txt = (t) => ({ shadow: { type: "text", fields: { TEXT: t } } });
    const shadowFor = (kind, value) => kind === "colour" ? { shadow: { type: "flx_colour", fields: { COLOUR: value } } }
      : kind === "key" ? { shadow: { type: "flx_key", fields: { KEY: value } } }
      : kind === "num" ? num(value) : txt(value);
    const block = (type, inputs, extra) => ({ kind: "block", type, ...(inputs ? { inputs } : {}), ...(extra || {}) });
    const fns = (category) => info.table.functions
      .filter((f) => f.category === category && f.toolbox !== false)
      .map((f) => block(blockType(f), Object.fromEntries((f.defaults || []).map((d, i) => ["ARG" + i, shadowFor(f.args[i], d)]))));
    const category = (name, contents, extra) => ({ kind: "category", name, colour: cats[name], contents, ...(extra || {}) });
    return { kind: "categoryToolbox", contents: [
      category("Say & Ask", [block("flx_say", { VALUE: txt("Hello!") }),
        block("flx_say_2", { VALUE0: txt("Score:"), VALUE1: num(10) }), block("flx_say_blank"), ...fns("Say & Ask")]),
      category("Variables", [], { custom: "FLX_VARIABLES" }),
      category("Control", [
        block("controls_if", { IF0: { block: { type: "flx_compare", fields: { OP: "GT" }, inputs: { A: num(5), B: num(3) } } } }),
        block("controls_if", null, { extraState: { hasElse: true } }),
        block("flx_repeat", { TIMES: num(10) }),
        block("flx_repeat_range", { FROM: num(1), TO: num(10) }),
        block("flx_repeat_each"),
        block("flx_repeat_while"),
        block("flx_forever"),
        block("flx_repeat_range_by", { FROM: num(10), TO: num(1), BY: num(-1) }),
        block("flx_stop"), block("flx_skip"),
      ]),
      category("Checks", [
        block("flx_compare", { A: num(1), B: num(2) }), block("flx_logic"), block("flx_not"),
        block("logic_boolean"),
      ]),
      category("Maths", [
        block("flx_number"), block("flx_arith", { A: num(1), B: num(1) }), block("flx_negate"), ...fns("Maths"),
      ]),
      category("Text", [block("text", null, { fields: { TEXT: "hello" } }), ...fns("Text")]),
      category("Lists", [
        block("lists_create_with", { ADD0: num(1), ADD1: num(2), ADD2: num(3) }, { extraState: { itemCount: 3 } }),
        block("lists_create_with", null, { extraState: { itemCount: 0 } }),
        block("flx_index", { INDEX: num(1) }), block("flx_length"), block("flx_copies", { COUNT: num(10), VALUE: num(0) }),
        block("flx_set_item", { INDEX: num(1) }), block("flx_add_item", { VALUE: num(1), INDEX: num(1) }),
        block("flx_subtract_item", { VALUE: num(1), INDEX: num(1) }), ...fns("Lists"),
      ]),
      category("Functions", [], { custom: "PROCEDURE" }),
      category("Drawing", [...fns("Drawing"), block("flx_colour")]),
      category("Keys & Mouse", [...fns("Keys & Mouse"), block("flx_key")]),
      category("Sound", fns("Sound")),
      category("Notes", [block("flx_note"), block("flx_return"), block("flx_return_nothing"),
                         block("flx_raw_stmt"), block("flx_raw_expr")]),
    ] };
  }

  function variableNames(w) {
    const map = w.getVariableMap ? w.getVariableMap() : null;
    const vars = map ? map.getAllVariables() : w.getAllVariables();
    return vars.map((x) => (x.getName ? x.getName() : x.name)).sort((a, b) => a.localeCompare(b));
  }

  function variablesFlyout(w) {
    const items = [{ kind: "button", text: "Make a variable", callbackkey: "FLX_NEW_VAR", callbackKey: "FLX_NEW_VAR" }];
    const names = variableNames(w);
    if (names.length) {
      const first = { VAR: { name: names[0] } };
      const num = (n) => ({ shadow: { type: "flx_number", fields: { NUM: String(n) } } });
      items.push({ kind: "block", type: "variables_set", fields: first, inputs: { VALUE: num(0) } });
      items.push({ kind: "block", type: "flx_add", fields: first, inputs: { VALUE: num(1) } });
      items.push({ kind: "block", type: "flx_subtract", fields: first, inputs: { VALUE: num(1) } });
      for (const name of names) items.push({ kind: "block", type: "variables_get", fields: { VAR: { name } } });
    }
    return items;
  }

  // ------------------------------------------------------------ themes
  function themes() {
    const cat = info.table.categories;
    const blockStyles = {
      variable_blocks: { colourPrimary: cat.Variables }, variable_dynamic_blocks: { colourPrimary: cat.Variables },
      procedure_blocks: { colourPrimary: cat.Functions }, logic_blocks: { colourPrimary: cat.Control },
      list_blocks: { colourPrimary: cat.Lists }, text_blocks: { colourPrimary: cat.Text },
      loop_blocks: { colourPrimary: cat.Control }, math_blocks: { colourPrimary: cat.Maths },
    };
    const font = { family: '"Segoe UI", system-ui, sans-serif', weight: "600", size: 12 };
    const light = Blockly.Theme.defineTheme("flaux-light", { name: "flaux-light", base: Blockly.Themes.Classic,
      blockStyles, fontStyle: font, componentStyles: {
        workspaceBackgroundColour: "#f7f8fc", toolboxBackgroundColour: "#ffffff", toolboxForegroundColour: "#1d2033",
        flyoutBackgroundColour: "#eceef7", flyoutForegroundColour: "#1d2033", flyoutOpacity: 1,
        scrollbarColour: "#c5c9da", insertionMarkerColour: "#000000", insertionMarkerOpacity: 0.25 } });
    const dark = Blockly.Theme.defineTheme("flaux-dark", { name: "flaux-dark", base: Blockly.Themes.Classic,
      blockStyles, fontStyle: font, componentStyles: {
        workspaceBackgroundColour: "#12141d", toolboxBackgroundColour: "#171a24", toolboxForegroundColour: "#e6e8f2",
        flyoutBackgroundColour: "#1c1f2c", flyoutForegroundColour: "#e6e8f2", flyoutOpacity: 1,
        scrollbarColour: "#3a3f55", insertionMarkerColour: "#ffffff", insertionMarkerOpacity: 0.3 } });
    return { light, dark };
  }

  // --------------------------------------------------------- generator
  function ident(name) {
    const s = String(name).trim().replace(/[^A-Za-z0-9_]+/g, "_").replace(/^_+|_+$/g, "");
    return !s ? "unnamed" : /^\d/.test(s) ? "_" + s : s;
  }

  function generate() {
    const lines = [];        // {text, id}
    const problems = [];     // {id, message}
    const empty = (b) => { problems.push({ id: b.id, message: "This block has an empty space. Drag a block into it." }); return "___"; };

    const varName = (b) => {
      const field = b.getField("VAR");
      const model = field && field.getVariable ? field.getVariable() : null;
      return ident(model ? (model.getName ? model.getName() : model.name) : field.getText());
    };
    const textCode = (raw) => {
      let t = String(raw).replace(/(^|[^\\])"/g, '$1\\"').replace(/(^|[^\\])"/g, '$1\\"');
      if (/(^|[^\\])(\\\\)*\\$/.test(t)) t += "\\";
      return '"' + t + '"';
    };
    const args = (b) => {
      const out = [];
      for (let i = 0; b.getInput("ARG" + i); i++) out.push(value(b, "ARG" + i));
      return out.join(", ");
    };
    const fnName = (b) => b.type.slice(3, b.type.lastIndexOf("_"));

    function value(b, input, need = 0) {
      const child = b.getInputTargetBlock(input);
      if (!child) return empty(b);
      const [code, prec] = expr(child);
      return prec < need ? `(${code})` : code;
    }

    function expr(b) {
      switch (b.type) {
        case "flx_number": { const t = b.getFieldValue("NUM").trim(); return [t, t.startsWith("-") ? 8 : 10]; }
        case "text": return [textCode(b.getFieldValue("TEXT")), 10];
        case "logic_boolean": return [b.getFieldValue("BOOL") === "TRUE" ? "true" : "false", 10];
        case "variables_get": return [varName(b), 10];
        case "lists_create_with": {
          const items = [];
          for (let i = 0; b.getInput("ADD" + i); i++) if (b.getInputTargetBlock("ADD" + i)) items.push(value(b, "ADD" + i));
          return ["[" + items.join(", ") + "]", 10];
        }
        case "flx_arith": {
          const op = b.getFieldValue("OP");
          if (op === "POW") return [`${value(b, "A", 10)} ^ ${value(b, "B", 8)}`, 9];
          const sym = { ADD: "+", SUB: "-", MUL: "*", DIV: "/", IDIV: "//", MOD: "mod" }[op];
          const p = op === "ADD" || op === "SUB" ? 6 : 7;
          return [`${value(b, "A", p)} ${sym} ${value(b, "B", p + 1)}`, p];
        }
        case "flx_compare": {
          const sym = { EQ: "=", NEQ: "!=", LT: "<", GT: ">", LTE: "<=", GTE: ">=" }[b.getFieldValue("OP")];
          return [`${value(b, "A", 5)} ${sym} ${value(b, "B", 5)}`, 4];
        }
        case "flx_logic":
          return b.getFieldValue("OP") === "AND" ? [`${value(b, "A", 2)} and ${value(b, "B", 3)}`, 2]
                                                  : [`${value(b, "A", 1)} or ${value(b, "B", 2)}`, 1];
        case "flx_not": return [`not ${value(b, "VALUE", 3)}`, 3];
        case "flx_negate": return [`-${value(b, "VALUE", 8)}`, 8];
        case "flx_index": return [`${value(b, "LIST", 10)}[${value(b, "INDEX")}]`, 10];
        case "flx_length": return [`length of ${value(b, "VALUE", 8)}`, 8];
        case "flx_copies": return [`${value(b, "COUNT", 6)} copies of ${value(b, "VALUE", 6)}`, 5];
        case "flx_colour": return [`"${b.getFieldValue("COLOUR")}"`, 10];
        case "flx_key": return [`"${b.getFieldValue("KEY")}"`, 10];
        case "flx_raw_expr": return [b.getFieldValue("CODE").trim() || empty(b), 0];
        case "procedures_callreturn": return [`${ident(b.getFieldValue("NAME"))}(${args(b)})`, 10];
        default:
          if (b.type.startsWith("fn_")) return [`${fnName(b)}(${args(b)})`, 10];
          problems.push({ id: b.id, message: "Flaux doesn't know this block." });
          return ["___", 10];
      }
    }

    function body(b, input, indent) {
      for (let c = b.getInputTargetBlock(input); c; c = c.getNextBlock()) stmt(c, indent);
    }

    function stmt(b, indent) {
      if (b.isEnabled && !b.isEnabled()) return;
      const first = lines.length;
      const emit = (text, more = 0) => lines.push({ text: "    ".repeat(indent + more) + text, id: b.id });
      switch (b.type) {
        case "flx_say": emit(`say ${value(b, "VALUE")}`); break;
        case "flx_say_blank": emit("say()"); break;
        case "flx_say_2": case "flx_say_3": case "flx_say_4": {
          const parts = [];
          for (let i = 0; b.getInput("VALUE" + i); i++) parts.push(value(b, "VALUE" + i));
          emit(`say ${parts.join(", ")}`); break;
        }
        case "flx_add_item": emit(`add ${value(b, "VALUE")} to ${value(b, "LIST", 10)}[${value(b, "INDEX")}]`); break;
        case "flx_subtract_item": emit(`subtract ${value(b, "VALUE")} from ${value(b, "LIST", 10)}[${value(b, "INDEX")}]`); break;
        case "variables_set": emit(`set ${varName(b)} to ${value(b, "VALUE")}`); break;
        case "flx_add": emit(`add ${value(b, "VALUE")} to ${varName(b)}`); break;
        case "flx_subtract": emit(`subtract ${value(b, "VALUE")} from ${varName(b)}`); break;
        case "flx_set_item": emit(`set ${value(b, "LIST", 10)}[${value(b, "INDEX")}] to ${value(b, "VALUE")}`); break;
        case "controls_if": {
          const st = (b.saveExtraState && b.saveExtraState()) || {};
          emit(`if ${value(b, "IF0")}`); body(b, "DO0", indent + 1);
          for (let i = 1; i <= (st.elseIfCount || 0); i++) { emit(`else if ${value(b, "IF" + i)}`); body(b, "DO" + i, indent + 1); }
          if (st.hasElse) { emit("else"); body(b, "ELSE", indent + 1); }
          emit("end"); break;
        }
        case "procedures_ifreturn":
          emit(`if ${value(b, "CONDITION")}`);
          emit(b.getInput("VALUE") && b.getInputTargetBlock("VALUE") ? `return ${value(b, "VALUE")}` : "return", 1);
          emit("end"); break;
        case "flx_repeat": emit(`repeat ${value(b, "TIMES")} times`); body(b, "DO", indent + 1); emit("end"); break;
        case "flx_repeat_range":
          emit(`repeat with ${varName(b)} from ${value(b, "FROM")} to ${value(b, "TO")}`);
          body(b, "DO", indent + 1); emit("end"); break;
        case "flx_repeat_range_by":
          emit(`repeat with ${varName(b)} from ${value(b, "FROM")} to ${value(b, "TO")} by ${value(b, "BY")}`);
          body(b, "DO", indent + 1); emit("end"); break;
        case "flx_repeat_each":
          emit(`repeat with ${varName(b)} in ${value(b, "LIST")}`); body(b, "DO", indent + 1); emit("end"); break;
        case "flx_repeat_while":
          emit(`repeat ${b.getFieldValue("MODE") === "UNTIL" ? "until" : "while"} ${value(b, "COND")}`);
          body(b, "DO", indent + 1); emit("end"); break;
        case "flx_forever": emit("repeat forever"); body(b, "DO", indent + 1); emit("end"); break;
        case "flx_stop": emit("stop"); break;
        case "flx_skip": emit("skip"); break;
        case "flx_return": emit(`return ${value(b, "VALUE")}`); break;
        case "flx_return_nothing": emit("return"); break;
        case "flx_note": emit("# " + b.getFieldValue("TEXT")); break;
        case "flx_raw_stmt": if (b.getFieldValue("CODE").trim()) emit(b.getFieldValue("CODE").trim()); break;
        case "procedures_defnoreturn": case "procedures_defreturn": {
          const saved = (b.saveExtraState && b.saveExtraState()) || {};
          const params = (saved.params ? saved.params.map((p) => p.name) : (b.arguments_ || [])).map(ident);
          emit(`function ${ident(b.getFieldValue("NAME"))}(${params.join(", ")})`);
          body(b, "STACK", indent + 1);
          if (b.type === "procedures_defreturn" && b.getInputTargetBlock("RETURN")) emit(`return ${value(b, "RETURN")}`, 1);
          emit("end"); break;
        }
        case "procedures_callnoreturn": emit(`${ident(b.getFieldValue("NAME"))}(${args(b)})`); break;
        default:
          if (b.type.startsWith("fn_")) emit(`${fnName(b)}(${args(b)})`);
          else problems.push({ id: b.id, message: "Flaux doesn't know this block." });
      }
      const note = b.getCommentText && b.getCommentText();
      if (note && note !== "Describe this function..." && lines[first])
        lines[first].text += "  # " + note.replace(/\s*\n\s*/g, " ");
    }

    const tops = ws.getTopBlocks(true).filter((b) => !b.outputConnection);
    tops.forEach((b, i) => {
      if (i && lines.length && lines[lines.length - 1].text !== "") lines.push({ text: "", id: null });
      for (let c = b; c; c = c.getNextBlock()) stmt(c, 0);
    });
    return { code: lines.map((l) => l.text).join("\n") + (lines.length ? "\n" : ""),
             ids: lines.map((l) => l.id), problems };
  }

  // -------------------------------------------------------------- public
  let themeSet = null;

  // Blockly's own blocks, in Flaux's words
  function words() {
    Object.assign(Blockly.Msg, {
      PROCEDURES_DEFNORETURN_TITLE: "function", PROCEDURES_DEFRETURN_TITLE: "function",
      PROCEDURES_DEFNORETURN_PROCEDURE: "do_something", PROCEDURES_DEFRETURN_PROCEDURE: "work_out",
      PROCEDURES_DEFRETURN_RETURN: "give back", PROCEDURES_BEFORE_PARAMS: "with inputs:",
      PROCEDURES_CALL_BEFORE_PARAMS: "with:", PROCEDURES_DEFNORETURN_COMMENT: "", PROCEDURES_DEFRETURN_COMMENT: "",
      PROCEDURES_MUTATORCONTAINER_TITLE: "inputs", PROCEDURES_MUTATORARG_TITLE: "input name:",
      LISTS_CREATE_WITH_INPUT_WITH: "list of", LISTS_CREATE_EMPTY_TITLE: "empty list",
      NEW_VARIABLE: "Make a variable", NEW_VARIABLE_TITLE: "Name of the new variable:",
      VARIABLES_SET: "set %1 to %2", LOGIC_BOOLEAN_TRUE: "true", LOGIC_BOOLEAN_FALSE: "false",
    });
  }

  function init(div, blockInfo, options) {
    info = blockInfo;
    onChange = options.onChange || onChange;
    words();
    defineBlocks();
    themeSet = themes();
    ws = Blockly.inject(div, {
      toolbox: toolbox(), renderer: "zelos", theme: options.dark ? themeSet.dark : themeSet.light,
      media: "blockly/media/", trashcan: true, sounds: true,
      zoom: { controls: true, wheel: false, startScale: 0.85, maxScale: 2.5, minScale: 0.35, scaleSpeed: 1.15 },
      move: { scrollbars: true, drag: true, wheel: true },
    });
    ws.registerToolboxCategoryCallback("FLX_VARIABLES", variablesFlyout);
    ws.registerButtonCallback("FLX_NEW_VAR", (button) => Blockly.Variables.createVariableButtonHandler(button.getTargetWorkspace()));
    ws.addChangeListener((e) => {
      if (loading || e.isUiEvent) return;
      onChange();
    });
    return ws;
  }

  function load(state) {
    loading = true;
    Blockly.Events.disable();          // loading isn't a change the user made
    try {
      Blockly.serialization.workspaces.load(state, ws);
    } finally {
      Blockly.Events.enable();
      loading = false;
    }
    ws.clearUndo();
    ws.scroll(0, 0);
  }

  function append(state) {
    const metrics = ws.getMetrics();
    let y = metrics.viewTop / ws.scale + 30;
    for (const b of state.blocks.blocks) {
      for (const v of state.variables || []) {
        const map = ws.getVariableMap ? ws.getVariableMap() : ws;
        if (!map.getVariable(v.name)) map.createVariable(v.name, "", v.id);
      }
      b.x = metrics.viewLeft / ws.scale + 40; b.y = y;
      const made = Blockly.serialization.blocks.append(b, ws);
      y += made.getHeightWidth().height + 30;
      made.select && made.select();
    }
  }

  function clearWarnings() {
    for (const b of ws.getAllBlocks(false)) if (b.warning || (b.getIcon && b.getIcon("warning"))) b.setWarningText(null);
  }

  function showProblem(id, message) {
    clearWarnings();
    const b = id && ws.getBlockById(id);
    if (!b) return;
    b.setWarningText(message);
    ws.centerOnBlock(id);
    b.select && b.select();
  }

  return {
    init, load, append, generate, showProblem, clearWarnings,
    setDark(dark) { if (ws) ws.setTheme(dark ? themeSet.dark : themeSet.light); },
    resize() { if (ws) Blockly.svgResize(ws); },
    zoom(step) { if (ws) ws.zoomCenter(step); },
    get workspace() { return ws; },
  };
})();
