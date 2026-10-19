#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Experimental, deliberately narrow Clang AST -> WGSL lowering.

Consumes upstream definitions, not regular-expression substitutions of C++.
Unsupported syntax/types fail closed. This is NOT a general C++ compiler.
"""
import json
import re
import subprocess
from pathlib import Path

FUNCTIONS = [
    'ray_triangle_reciprocal', 'ray_triangle_dot', 'ray_triangle_cross',
    'ray_triangle_intersect', 'fractf', 'wrapf', 'pingpongf', 'smoothminf',
    'signf', 'compatible_atan2', 'compatible_signf', 'safe_sqrtf',
    'inversesqrtf', 'safe_asinf', 'safe_acosf', 'compatible_powf',
    'safe_powf', 'safe_divide', 'safe_logf', 'safe_modulo',
    'safe_floored_modulo', 'svm_math', 'svm_math_gamma_color',
]
BUILTINS = {
    'make_float2': 'vec2<f32>', 'make_float3': 'vec3<f32>', 'make_float4': 'vec4<f32>',
    'dot': 'dot', 'min': 'min', 'max': 'max', 'clamp': 'clamp',
    'fabsf': 'abs', 'fminf': 'min', 'fmaxf': 'max', 'sqrtf': 'sqrt',
    'floorf': 'floor', 'ceilf': 'ceil', 'sinf': 'sin', 'cosf': 'cos', 'tanf': 'tan',
    'asinf': 'c_asin', 'acosf': 'c_acos', 'atanf': 'atan', 'atan2f': 'atan2',
    'sinhf': 'sinh', 'coshf': 'cosh', 'tanhf': 'tanh', 'expf': 'exp', 'logf': 'log',
    'powf': 'c_pow', 'fmodf': 'c_fmod', 'float_to_int': 'i32',
}
PRELUDE = '''// Generated from Blender Cycles; see provenance.json and THIRD-PARTY-NOTICES.txt.
// Original code: SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-License-Identifier: Apache-2.0
// wrapf retains upstream's attribution: Adapted from godot-engine math_funcs.h.
// C library adapters below: Copyright (c) 2026 Jeremy Ellis and contributors
fn c_asin_series(x: f32) -> f32 {
    // Taylor coefficients binomial(2n,n)/(4^n*(2n+1)), n=0..11.
    // Half-angle reduction below keeps |x| <= 0.5; omitted tail < 4e-10.
    let z = x*x;
    var term = x;
    var sum = x;
    for (var n=1u; n<12u; n++) {
        let k = f32(n);
        term *= z*((2.0*k-1.0)*(2.0*k-1.0))/((2.0*k)*(2.0*k+1.0));
        sum += term;
    }
    return sum;
}
fn c_asin(x: f32) -> f32 {
    let a = abs(x);
    if (a <= 0.5) { return c_asin_series(x); }
    let result = 1.5707963267948966 - 2.0*c_asin_series(sqrt((1.0-a)*0.5));
    return select(result, -result, x < 0.0);
}
fn c_acos(x: f32) -> f32 {
    if (x >= 0.5) { return 2.0*c_asin_series(sqrt((1.0-x)*0.5)); }
    if (x <= -0.5) { return 3.141592653589793 - 2.0*c_asin_series(sqrt((1.0+x)*0.5)); }
    return 1.5707963267948966-c_asin_series(x);
}
fn c_fmod(a: f32, b: f32) -> f32 { return a - trunc(a / b) * b; }
fn c_pow(a: f32, b: f32) -> f32 {
    // C pow accepts negative bases with integer exponents; WGSL pow does not.
    if (a < 0.0) {
        let p = pow(-a, b);
        if (c_fmod(abs(b), 2.0) == 0.0) { return p; }
        return -p;
    }
    return pow(a, b);
}
'''

def nodes(n):
    yield n
    for child in n.get('inner', []):
        yield from nodes(child)

def ast(upstream, work, name, translation_unit):
    output = work / (name.replace(':', '_') + '.ast.json')
    command = ['clang++', '-std=c++20', '-fsyntax-only', '-Xclang', '-ast-dump=json',
               '-Xclang', '-ast-dump-filter=ccl::' + name, '-I', str(upstream / 'src'),
               str(translation_unit)]
    with output.open('w') as stream:
        subprocess.run(command, stdout=stream, check=True)
    text = output.read_text()
    decoder = json.JSONDecoder()
    values = []
    while text.strip():
        item, end = decoder.raw_decode(text.lstrip())
        values.append(item)
        text = text.lstrip()[end:]
    return values

def type_of(n):
    t = n['type']
    return t.get('desugaredQualType', t['qualType'])

def wgtype(t):
    t = t.replace('ccl::', '').replace('const ', '').replace('__restrict', '').strip()
    table = {'float': 'f32', 'int': 'i32', 'unsigned int': 'u32',
             'uint': 'u32', 'bool': 'bool', 'void': '', 'NodeMathType': 'u32',
             'float2': 'vec2<f32>', 'float3': 'vec3<f32>', 'float4': 'vec4<f32>'}
    if t in table:
        return table[t]
    if t.endswith('*'):
        return 'ptr<function, ' + wgtype(t[:-1].strip()) + '>'
    raise ValueError('Unsupported type: ' + t)

class Emitter:
    def __init__(self):
        self.pending = []
        self.serial = 0
        self.calls = set()

    def value(self, n):
        kind, children = n['kind'], n.get('inner', [])
        if kind in ('ParenExpr', 'ExprWithCleanups', 'MaterializeTemporaryExpr'):
            return '(' + self.value(children[0]) + ')'
        if kind == 'CXXConstructExpr':
            if len(children) == 1 and wgtype(type_of(n)) == wgtype(type_of(children[0])):
                return self.value(children[0])
            return wgtype(type_of(n)) + '(' + ', '.join(self.value(x) for x in children) + ')'
        if kind in ('ImplicitCastExpr', 'CStyleCastExpr', 'CXXStaticCastExpr', 'CXXFunctionalCastExpr'):
            child = children[0]
            value = self.value(child)
            cast = n.get('castKind')
            if cast in ('NoOp', 'LValueToRValue', 'FunctionToPointerDecay', 'BuiltinFnToFnPtr'):
                return value
            if cast not in ('IntegralCast', 'FloatingCast', 'IntegralToFloating',
                            'FloatingToIntegral', 'IntegralToBoolean', 'FloatingToBoolean'):
                raise ValueError('Unsupported cast: ' + str(cast))
            target, source = wgtype(type_of(n)), wgtype(type_of(child))
            if target == source:
                return value
            if target == 'bool':
                return '(' + value + ' != ' + source + '(0))'
            if source == 'bool':
                return 'select(' + target + '(0), ' + target + '(1), ' + value + ')'
            return target + '(' + value + ')'
        if kind in ('FloatingLiteral', 'IntegerLiteral', 'ConstantExpr') and 'value' in n:
            value = str(n['value'])
            if wgtype(type_of(n)) == 'f32':
                if '.' not in value and 'e' not in value.lower():
                    value += '.0'
                return value + 'f'
            return value + ('u' if wgtype(type_of(n)) == 'u32' else 'i')
        if kind == 'CXXBoolLiteralExpr':
            return str(n['value']).lower()
        if kind == 'DeclRefExpr':
            ref = n['referencedDecl']
            if ref['kind'] == 'FunctionDecl':
                return ref['name']
            return 'v_' + ref['name']
        if kind == 'MemberExpr':
            return self.value(children[0]) + '.' + n['name']
        if kind == 'UnaryOperator':
            if n['opcode'] in ('++', '--'):
                raise ValueError('Increment is supported only as a statement')
            return '(' + n['opcode'] + self.value(children[0]) + ')'
        if kind == 'BinaryOperator':
            if n['opcode'] in ('=', '+=', '-='):
                raise ValueError('Assignment expression requires statement lowering')
            left = self.value(children[0])
            before = self.pending
            self.pending = []
            right = self.value(children[1])
            extra = self.pending
            self.pending = before
            if extra and n['opcode'] in ('&&', '||'):
                raise ValueError('Short circuit with a statement-producing RHS requires explicit lowering')
            self.pending += extra
            return '(' + left + ' ' + n['opcode'] + ' ' + right + ')'
        if kind == 'ConditionalOperator':
            condition = self.value(children[0])
            previous = self.pending
            self.pending = []
            yes = self.value(children[1]); yes_lines = self.pending
            self.pending = []
            no = self.value(children[2]); no_lines = self.pending
            self.pending = previous
            self.serial += 1
            name = 'choice_' + str(self.serial)
            self.pending += [f'var {name}: {wgtype(type_of(n))};', f'if ({condition}) {{',
                *yes_lines, f'{name} = {yes};', '} else {', *no_lines, f'{name} = {no};', '}']
            return name
        if kind in ('CallExpr', 'CXXOperatorCallExpr'):
            name = self.value(children[0])
            args = [self.value(c) for c in children[1:]]
            if name == '__builtin_expect':
                return args[0]
            if name.startswith('operator'):
                op = name[len('operator'):]
                if op not in ('+', '-', '*', '/', '==', '!='):
                    raise ValueError('Unsupported vector operator: ' + name)
                return '(' + ((' ' + op + ' ').join(args) if len(args) == 2 else op + args[0]) + ')'
            if name in BUILTINS:
                return BUILTINS[name] + '(' + ', '.join(args) + ')'
            if name not in FUNCTIONS:
                raise ValueError('Unmapped call: ' + name)
            self.calls.add(name)
            return 'c_' + name + '(' + ', '.join(args) + ')'
        raise ValueError('Unsupported expression: ' + kind)

    def expression(self, n):
        self.pending = []
        value = self.value(n)
        lines = self.pending
        self.pending = []
        return lines, value

    def statement(self, n):
        kind, children = n['kind'], n.get('inner', [])
        if kind == 'CompoundStmt':
            return [line for child in children for line in self.statement(child)]
        if kind == 'DeclStmt':
            lines = []
            for var in children:
                initializers = [x for x in var.get('inner', []) if not x['kind'].endswith('Attr')]
                if initializers:
                    prefix, value = self.expression(initializers[-1])
                    lines += prefix + [f'var v_{var["name"]}: {wgtype(type_of(var))} = {value};']
                else:
                    lines += [f'var v_{var["name"]}: {wgtype(type_of(var))};']
            return lines
        if kind == 'ReturnStmt':
            prefix, value = self.expression(children[0])
            return prefix + ['return ' + value + ';']
        if kind == 'IfStmt':
            if len(children) not in (2, 3):
                raise ValueError('Unsupported if initializer')
            prefix, cond = self.expression(children[0])
            lines = prefix + ['if (' + cond + ') {'] + self.statement(children[1]) + ['}']
            if len(children) == 3:
                lines += ['else {'] + self.statement(children[2]) + ['}']
            return lines
        if kind == 'SwitchStmt':
            prefix, cond = self.expression(children[0])
            lines = prefix + ['switch (' + cond + ') {']
            for case in children[1]['inner']:
                if case['kind'] == 'CaseStmt':
                    p, label = self.expression(case['inner'][0])
                    if p: raise ValueError('Nonconstant case')
                    lines += ['case ' + label + ': {'] + self.statement(case['inner'][1]) + ['}']
                elif case['kind'] == 'DefaultStmt':
                    lines += ['default: {'] + self.statement(case['inner'][0]) + ['}']
                else:
                    raise ValueError('Unsupported switch fallthrough/statement: ' + case['kind'])
            return lines + ['}']
        if kind == 'BinaryOperator' and n['opcode'] == '=':
            left_p, left = self.expression(children[0]); right_p, right = self.expression(children[1])
            return left_p + right_p + [left + ' = ' + right + ';']
        raise ValueError('Unsupported statement: ' + kind)

    def function(self, fn):
        params = [x for x in fn['inner'] if x['kind'] == 'ParmVarDecl']
        body = next(x for x in fn['inner'] if x['kind'] == 'CompoundStmt')
        ret = wgtype(fn['type']['qualType'].split(' (')[0])
        signature, locals_ = [], []
        for param in params:
            typ = wgtype(type_of(param)); name = param['name']
            if typ.startswith('ptr<'):
                signature += [f'v_{name}: {typ}']
            else:
                signature += [f'in_{name}: {typ}']
                locals_ += [f'var v_{name} = in_{name};']
        lines = [f'fn c_{fn["name"]}(' + ', '.join(signature) + ') -> ' + ret + ' {']
        lines += locals_ + self.statement(body) + ['}']
        return '\n'.join(lines)

def generate(upstream, work, translation_unit):
    emitter = Emitter(); output = [PRELUDE]; manifest = []
    for name in FUNCTIONS:
        candidates = [n for top in ast(upstream, work, name, translation_unit) for n in nodes(top)
                      if n['kind'] == 'FunctionDecl' and n.get('name') == name
                      and any(x['kind'] == 'CompoundStmt' for x in n.get('inner', []))]
        # The probe uses only scalar overloads of safe_divide; other selected names are unique.
        if name == 'safe_divide':
            candidates = [n for n in candidates if n['type']['qualType'] == 'float (const float, const float)']
        if len(candidates) != 1:
            raise ValueError(f'{name}: expected one definition, found {len(candidates)}')
        fn = candidates[0]
        output.append(emitter.function(fn))
        manifest.append({'name': name, 'signature': fn['type']['qualType'],
                         'astKinds': sorted(set(n['kind'] for n in nodes(fn)))})
    (work / 'translated.wgsl').write_text('\n\n'.join(output) + '\n')
    (work / 'translation.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return '\n\n'.join(output)

if __name__ == '__main__':
    import sys
    root = Path(__file__).resolve().parent
    generate(Path(sys.argv[1]), root / 'work', root / 'native/reference.cpp')
