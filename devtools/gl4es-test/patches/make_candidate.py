#!/usr/bin/env python3
"""Applies the candidate fixes for the bugs in FINDINGS.md to a gl4es tree (a744af14 + ship-fixes.patch),
in place. Each fix is checked to apply exactly once. Then `git diff > candidate-fixes.patch`.
usage: make_candidate.py <gl4es tree>"""
import re, sys, os

root = sys.argv[1]


def edit(path, old, new, count=1):
    p = os.path.join(root, path)
    s = open(p, encoding='utf-8', errors='surrogateescape').read()
    n = s.count(old)
    if n != count:
        sys.exit(f'{path}: expected {count} match(es), found {n}: {old[:60]!r}')
    s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)


# F2: ARB SIN: 9 bytes copied from a 4-byte literal
edit('src/gl/arbgenerator.c', 'APPEND_OUTPUT("sin(", 9)', 'APPEND_OUTPUT("sin(", 4)')

# F10: resize() returned the new size in bytes as the capacity in elements: heap overflow in pushArray
edit('src/gl/arbhelper.c', '''		*obj = reloc;
		*cap = newSize;''', '''		*obj = reloc;
		*cap = newSize / esize;''')

# F3: state.light[n].<prop>: buffer 10 bytes (more for spotDirection) too small for "gl_LightSource[%s].%s"
edit('src/gl/arbparser.c', '''					matrixNameMallocd = (char*)malloc((mtxNameLen + strlen(sln) + 8) * sizeof(char));
					sprintf(matrixNameMallocd, "gl_LightSource[%s].%s", sln, matrixName);''',
     '''					matrixNameMallocd = (char*)malloc((strlen(matrixName) + strlen(sln) + 18) * sizeof(char));
					sprintf(matrixNameMallocd, "gl_LightSource[%s].%s", sln, matrixName);''')

# F4: result.color.front / result.color.back with nothing after: next token is NULL -> strcmp(NULL)
edit('src/gl/arbparser.c', '''				if (IS_NONE_OR_SWIZZLE) {
					// result.color.front => gl_FrontColor
					pushArray((sArray*)&newVar->var->init, strdup("gl_FrontColor"));
					newVar->var->init.strings_total_len = 13;
				}''', '''				if (IS_NONE_OR_SWIZZLE) {
					// result.color.front => gl_FrontColor
					pushArray((sArray*)&newVar->var->init, strdup("gl_FrontColor"));
					newVar->var->init.strings_total_len = 13;
					return 0;
				}''')
edit('src/gl/arbparser.c', '''				if (IS_NONE_OR_SWIZZLE) {
					// result.color.back => gl_BackColor
					pushArray((sArray*)&newVar->var->init, strdup("gl_BackColor"));
					newVar->var->init.strings_total_len = 12;
				}''', '''				if (IS_NONE_OR_SWIZZLE) {
					// result.color.back => gl_BackColor
					pushArray((sArray*)&newVar->var->init, strdup("gl_BackColor"));
					newVar->var->init.strings_total_len = 12;
					return 0;
				}''')

# F1: program binds and program parameters must end a pending glBegin/glEnd batch first
p = os.path.join(root, 'src/gl/oldprogram.c')
s = open(p, encoding='utf-8').read()
pat = re.compile(r'(void APIENTRY_GL4ES gl4es_gl(?:BindProgramARB|Program(?:Env|Local)Parameter(?:4[df]v?ARB|s4fvEXT))\([^)]*\)\s*\{\n)')
s, n = pat.subn(lambda m: m.group(1) + '    FLUSH_BEGINEND;\n', s)
if n != 11:
    sys.exit(f'oldprogram.c: expected 11 functions to flush, found {n}')
open(p, 'w', encoding='utf-8', newline='').write(s)

# F11: GL_UNPACK_ROW_LENGTH rows ignored GL_UNPACK_ALIGNMENT
edit('src/gl/texture.c', '''imgWidth = ((glstate->texture.unpack_row_length)? glstate->texture.unpack_row_length:width) * pixelSize;''',
     '''imgWidth = ((glstate->texture.unpack_row_length)? glstate->texture.unpack_row_length:width) * pixelSize;
            imgWidth = (imgWidth + glstate->texture.unpack_align - 1) / glstate->texture.unpack_align * glstate->texture.unpack_align;''', count=2)
# F6/F17: glTexCoord/glMultiTexCoord inside glBegin/glEnd were dropped for units without texturing enabled,
# also when a GLSL program (glMultiTexCoord only) or an ARB program reads them
edit('src/gl/gl4es.c', '''if(hardext.esversion==1 || glstate->glsl->program || (glstate->list.begin && (glstate->list.compiling || glstate->enable.texture[0])))''',
     '''if(hardext.esversion==1 || glstate->glsl->program || glstate->enable.vertex_arb || glstate->enable.fragment_arb || (glstate->list.begin && (glstate->list.compiling || glstate->enable.texture[0])))''')
edit('src/gl/gl4es.c', '''if(hardext.esversion==1 || (glstate->list.begin && (glstate->list.compiling || glstate->enable.texture[target-GL_TEXTURE0])))''',
     '''if(hardext.esversion==1 || glstate->glsl->program || glstate->enable.vertex_arb || glstate->enable.fragment_arb || (glstate->list.begin && (glstate->list.compiling || glstate->enable.texture[target-GL_TEXTURE0])))''', count=3)
# F18: appendString strcpy'd the whole C string whatever strLen said (APPEND_OUTPUT(&"0123456789"[i], 1) copies up
# to 10 bytes): heap write past the output buffer; also no room was kept for the terminating NUL
edit('src/gl/arbhelper.c', '''	if (curStatusPtr->outLeft < strLen) {
		char *oldOut = curStatusPtr->outputString;
		while (curStatusPtr->outLeft < strLen) {''', '''	if (curStatusPtr->outLeft < strLen + 1) {
		char *oldOut = curStatusPtr->outputString;
		while (curStatusPtr->outLeft < strLen + 1) {''')
edit('src/gl/arbhelper.c', '''	strcpy(curStatusPtr->outputEnd, str);
	curStatusPtr->outLen += strLen;''', '''	memcpy(curStatusPtr->outputEnd, str, strLen);
	curStatusPtr->outputEnd[strLen] = '\\0';
	curStatusPtr->outLen += strLen;''')

# F19: fragment program locals: 24 slots, but programs with more are accepted (Unity's shadow collector uses 27):
# builtin_CheckUniform wrote past frg_progloc[24] into the program struct
edit('src/config.h', '#define MAX_FRG_PROG_LOC_PARAMS 24', '#define MAX_FRG_PROG_LOC_PARAMS 64')
edit('src/gl/fpe.c', '''        glprogram->has_frg_progloc = 1;
        for (int i=0; i<n; ++i)
            glprogram->frg_progloc[i] = id+i;''', '''        glprogram->has_frg_progloc = 1;
        for (int i=0; i<n && i<MAX_FRG_PROG_LOC_PARAMS; ++i)
            glprogram->frg_progloc[i] = id+i;''')
edit('src/gl/fpe.c', '''        glprogram->has_vtx_progloc = 1;
        for (int i=0; i<n; ++i)
            glprogram->vtx_progloc[i] = id+i;''', '''        glprogram->has_vtx_progloc = 1;
        for (int i=0; i<n && i<MAX_VTX_PROG_LOC_PARAMS; ++i)
            glprogram->vtx_progloc[i] = id+i;''')
# F20: the ROW_LENGTH/SKIP repack wrote tight rows, but every conversion after it still applies GL_UNPACK_ALIGNMENT:
# rows misread, heap over-read past the repacked copy (ASan). Repack rows padded to that alignment instead.
p = os.path.join(root, 'src/gl/texture.c')
s = open(p, encoding='utf-8').read()
s, n = re.subn(r'\n( +)GLubyte \*dst = \(GLubyte \*\)malloc\(width \* height \* pixelSize\);',
               lambda m: f'\n{m.group(1)}dstWidth = (width * pixelSize + glstate->texture.unpack_align - 1) / glstate->texture.unpack_align * glstate->texture.unpack_align;'
                         f'\n{m.group(1)}GLubyte *dst = (GLubyte *)malloc(dstWidth * height);', s)
if n != 2:
    sys.exit(f'texture.c: expected 2 repack mallocs, found {n}')
s = re.sub(r'\n( +)imgWidth = \(\(glstate->texture\.unpack_row_length\)\? glstate->texture\.unpack_row_length:width\) \* pixelSize;\n +imgWidth = \(imgWidth',
           lambda m: m.group(0).replace('\n            imgWidth = (imgWidth', '\n' + m.group(1) + 'imgWidth = (imgWidth'), s)
open(p, 'w', encoding='utf-8', newline='').write(s)
edit('src/gl/texture.c', '''dstWidth = width * pixelSize;
''', '''// dstWidth: aligned rows, see above
''', count=2)
edit('src/gl/texture.c', '''memcpy(dst, src, dstWidth);''', '''memcpy(dst, src, width * pixelSize);''', count=2)
print('candidate fixes applied')
