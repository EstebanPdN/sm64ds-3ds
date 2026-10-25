"""Preserve da1up's recovered block-local C function views under GCC.

MWCC accepts differing local declarations of one C symbol; GCC rejects the
intact TU. Give each local declaration a distinct compiler identifier, with
the original assembler symbol and unchanged parameter/return types. Calls and
address expressions keep their local view. No function body, argument or class
layout is rewritten. The source transformation must invert byte for byte.
"""
import re

def mask(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',
        lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)

def function_ranges(text):
    code = mask(text)
    braces, parens, pairs, roots = [], [], {}, []
    for i, c in enumerate(code):
        if c == '(': parens.append(i)
        elif c == ')': pairs[i] = parens.pop()
        elif c == '{': braces.append(i)
        elif c == '}':
            start = braces.pop()
            before = code[:start].rstrip()
            if before.endswith(')'):
                opening = pairs[len(before)-1]
                match = re.search(r'([\w:~]+)\s*$', code[:opening])
                if match and match[1] not in ('if','for','while','switch','catch'):
                    roots.append((start, i+1, match[1]))
    assert not braces and not parens
    return sorted(roots)

def adapt_da1up(text):
    assert len(re.findall(r'^// @symbol ',text,re.M)) == 38
    roots = function_ranges(text)
    assert len(roots) == 37, ('da1up function census changed',len(roots))
    edits, rows = [], []
    declaration = re.compile(r'\bextern\s+[^;{}]+?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;')
    for index, (start, end, function) in enumerate(roots):
        body = text[start:end]
        matches = list(declaration.finditer(body))
        names = [m[1] for m in matches]
        assert len(set(names)) == len(names), (function,'duplicate local function view')
        if not matches: continue
        aliases = {name:f'sm64ds_da1up_local_{index}_{i}' for i,name in enumerate(names)}
        assert all(alias not in text for alias in aliases.values())
        # Replace only identifier tokens outside comments and strings.
        code = mask(body)
        tokens = [m for m in re.finditer(r'\b[A-Za-z_]\w*\b',code) if m[0] in aliases]
        transformed = body
        for token in reversed(tokens):
            transformed = transformed[:token.start()]+aliases[token[0]]+transformed[token.end():]
        for name, alias in aliases.items():
            prototype = re.compile(r'(\bextern\s+[^;{}]+?\b'+re.escape(alias)+r'\s*\([^;{}]*\))\s*;')
            transformed, count = prototype.subn(lambda m:m[1]+f' asm("{name}");',transformed)
            assert count == 1, (function,name,count)
            rows.append(dict(function=function,original_symbol=name,alias=alias,
                source_line=text[:start].count('\n')+body[:matches[names.index(name)].start()].count('\n')+1))
        inverse = transformed
        for name, alias in aliases.items():
            inverse = inverse.replace(f' asm("{name}");',';',1)
            inverse = re.sub(r'\b'+re.escape(alias)+r'\b',name,inverse)
        # Trailing prototype whitespace is kept separately in the inverse.
        if inverse != body:
            for match in matches:
                original = match[0]
                normalized = re.sub(r'\)\s*;$',');',original)
                if normalized != original: inverse = inverse.replace(normalized,original,1)
        assert inverse == body, ('non-invertible local declaration bridge',function)
        edits.append((start,end,transformed))
    result = text
    for start,end,replacement in reversed(edits): result=result[:start]+replacement+result[end:]
    assert rows, 'No local views found'
    return result, rows

def adapt_mip_data(text):
    """Keep each of MIPS's recovered local data types and original symbols.

    A local class cannot type an external GCC object. Bind a typed lvalue view
    to an assembler-named byte address instead. This also separates conflicting
    scalar/array/struct views without widening or narrowing any access. The
    references allocate no game storage and preserve sizeof/decay/address use.
    """
    assert len(re.findall(r'^// @symbol ',text,re.M)) == 32
    roots = function_ranges(text)
    assert len(roots) == 31, ('MIPS function census changed',len(roots))
    declaration = re.compile(r'\bextern\s+([^;{}()]+?)\b(data_\w+)\b(\s*(?:\[[^\];]*\]\s*)*)\s*;')
    edits, rows = [], []
    for index,(start,end,function) in enumerate(roots):
        body = text[start:end]
        matches = list(declaration.finditer(body))
        names = [m[2] for m in matches]
        assert len(set(names)) == len(names), (function,'duplicate local data view')
        aliases = {name:f'sm64ds_mip_view_{index}_{i}' for i,name in enumerate(names)}
        assert all(alias not in text for alias in aliases.values())
        transformed = body
        replacements = []
        for i,match in reversed(list(enumerate(matches))):
            typename,name,array = match.groups()
            typename = typename.strip()
            storage = f'sm64ds_mip_storage_{index}_{i}'
            pointer_type = typename+(' (*)'+array.strip() if array.strip() else ' *')
            replacement = f'extern unsigned char {storage}[] asm("{name}");\n    auto &{name} = *reinterpret_cast<{pointer_type}>({storage});'
            transformed = transformed[:match.start()]+replacement+transformed[match.end():]
            replacements.append((replacement,match[0]))
            rows.append({'function':function,'original_symbol':name,'type':typename+array,
                'alias':aliases[name],'storage':storage})
        code = mask(transformed)
        tokens = [m for m in re.finditer(r'\b[A-Za-z_]\w*\b',code) if m[0] in aliases]
        for token in reversed(tokens):
            transformed = transformed[:token.start()]+aliases[token[0]]+transformed[token.end():]
        inverse = transformed
        for name,alias in aliases.items(): inverse = re.sub(r'\b'+re.escape(alias)+r'\b',name,inverse)
        for replacement,original in replacements:
            assert inverse.count(replacement) == 1
            inverse = inverse.replace(replacement,original,1)
        assert inverse == body, ('non-invertible MIPS data view',function)
        if matches: edits.append((start,end,transformed))
    result = text
    for start,end,replacement in reversed(edits): result=result[:start]+replacement+result[end:]
    assert rows, 'No MIPS data views found'
    return result, rows
