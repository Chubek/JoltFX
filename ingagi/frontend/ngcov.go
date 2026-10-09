package frontend

import (
	_ "embed"
	"regexp"
	"sort"
)

//go:embed ingagi.ng
var ingagiNG string

// NGGrammar returns the draft NovoParse grammar source (ingagi.ng).
// Validate it with the vendored tool: `novoparse check ingagi.ng`.
func NGGrammar() string { return ingagiNG }

// NGRuleCoverage maps every rule in ingagi.ng to the Go parse function that
// corresponds to it. This map tracks rule names only; it cannot verify syntax
// equivalence. Native parsing tests exercise the generated grammar separately.
var NGRuleCoverage = map[string]string{
	"add_expr": "parseAdd", "and_expr": "parseAnd",
	"arg": "parseArg", "arg_list": "parsePostfix",
	"assign_expr": "parseAssign", "assign_op": "assignOps",
	"attribute": "parseAttrList", "attr_list": "parseAttrList",
	"bind_stmt": "parseBind", "bitand_expr": "parseBitand",
	"bitor_expr": "parseBitor", "bitxor_expr": "parseBitxor",
	"block": "parseBlock", "break_stmt": "parseBreak",
	"compilation_unit": "ParseFile", "component_attach": "parseEntityDecl",
	"component_decl": "parseComponentDecl", "component_field": "parseComponentField",
	"component_member": "parseComponentDecl", "const_decl": "parseConstDecl",
	"continue_stmt": "parseContinue", "defer_stmt": "parseStmt",
	"emit_stmt": "parseStmt", "entity_child": "parseEntityDecl",
	"entity_decl": "parseEntityDecl", "entity_expr": "parseEntityExpr",
	"entity_member": "parseEntityDecl", "entity_prop": "parseEntityDecl",
	"enum_decl": "parseEnumDecl", "enum_variant": "parseEnumDecl",
	"enum_variants": "parseEnumDecl", "eq_expr": "parseEq",
	"event_decl": "parseEventDecl", "expr": "parseExpr",
	"extend_decl": "parseExtendDecl", "external_decl": "parseExternalDecl",
	"extern_body": "parseExternalDecl", "extern_decl": "parseExternDecl",
	"foreach_stmt": "parseFor", "for_stmt": "parseFor",
	"func_decl": "parseFnDecl", "generic_args": "parseType",
	"generic_params": "parseGenericParams", "hook_name": "parseSystemHook",
	"if_expr": "parseIfExpr", "if_stmt": "parseIfStmt",
	"import_decl": "ParseFile", "interface_decl": "parseInterfaceDecl",
	"lambda_expr": "parseLambda", "literal": "parseLit",
	"loop_stmt": "parseStmt", "match_arm": "parseMatchArms",
	"match_arms": "parseMatchArms", "match_stmt": "parseMatchStmt",
	"module_decl": "ParseFile", "mul_expr": "parseMul",
	"on_stmt": "parseOn", "or_expr": "parseOr",
	"param": "parseParam", "param_list": "parseFnDecl",
	"param_type_list": "parseType", "pattern": "parsePattern",
	"pattern_field": "parsePattern", "pattern_fields": "parsePattern",
	"pipeline_decl": "parsePipelineDecl", "pipeline_member": "parsePipelineDecl",
	"postfix_expr": "parsePostfix", "postfix_suffix": "parsePostfix",
	"primary_expr": "parsePrimary", "qualified_name": "parsePath",
	"qualified_type": "parseType", "query_binding": "parseQueryBinding",
	"query_bindings": "parseFor", "query_comp": "parseQueryComp",
	"query_comps": "parseQueryComps", "query_filter": "parseSystemQuery",
	"rel_expr": "parseRel", "resource_decl": "parseResourceDecl",
	"return_stmt": "parseReturn", "shader_const": "parseShaderConst",
	"shader_decl": "parseShaderDecl", "shader_field": "parseShaderField",
	"shader_func": "parseShaderFunc", "shader_member": "parseShaderDecl",
	"shader_option": "parseShaderDecl", "shader_options": "parseShaderDecl",
	"shader_param": "parseShaderFunc", "shader_params": "parseShaderFunc",
	"shader_resource": "tryParseShaderResource", "shader_resource_body": "tryParseShaderResource",
	"shader_spec": "parseShaderDecl", "shader_stage": "parseShaderStage",
	"shader_struct": "parseShaderStruct", "shader_type": "parseShaderType",
	"shift_expr": "parseShift", "state_assign": "parsePipelineDecl",
	"stmt": "parseStmt", "struct_decl": "parseStructDecl",
	"struct_init": "parseStructInit", "struct_inits": "parsePrimary",
	"struct_member": "parseStructDecl", "system_decl": "parseSystemDecl",
	"system_dep": "parseSystemDep", "system_hook": "parseSystemHook",
	"system_member": "parseSystemDecl", "system_query": "parseSystemQuery",
	"ternary_expr": "parseTernary", "texture_kind": "tryParseShaderResource",
	"type_alias_decl": "parseTypeAlias", "type_expr": "parseType",
	"type_list": "parseTypeList", "unary_expr": "parseUnary",
	"unsafe_stmt": "parseStmt", "var_decl_stmt": "parseVarDecl",
	"visibility": "parseVisibility", "while_stmt": "parseWhile",
}

var ngRuleRe = regexp.MustCompile(`(?m)^([A-Za-z_][A-Za-z0-9_]*)\s*:`)

// NGRules extracts rule names from the embedded grammar.
func NGRules() []string {
	var out []string
	seen := map[string]bool{}
	for _, m := range ngRuleRe.FindAllStringSubmatch(ingagiNG, -1) {
		if !seen[m[1]] {
			seen[m[1]] = true
			out = append(out, m[1])
		}
	}
	sort.Strings(out)
	return out
}
