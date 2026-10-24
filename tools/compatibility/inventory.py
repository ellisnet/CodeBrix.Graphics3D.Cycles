#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Source-derived compatibility ledger; compiler metadata is not rendering evidence."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from cycles_source import REVISION, SOURCE, verify as verify_cycles

DIRECTORY = ROOT / 'compatibility'
HEADERS = ('kernel/svm/types.h', 'kernel/types.h', 'util/types_image.h')
INPUTS = (*HEADERS, 'kernel/svm/node_types_template.h', 'kernel/features.h',
          'kernel/data_arrays.h')
RIDS = ('win-x64', 'win-arm64', 'osx-arm64', 'osx-x64', 'linux-x64',
        'linux-arm64', 'linux-riscv64', 'android-arm64', 'android-x64')
# The GPU backend is fixed per RID: Cycles' own Metal device on macOS on Apple
# Silicon, the CodeBrix WGSL device on WebGPU everywhere else (osx-x64 included).
METAL_RIDS = ('osx-arm64',)
METAL_STATUS = 'native/host/MACOS_ARM64.md'
FAMILIES = {'ShaderNodeType': 'opcodes', 'ClosureType': 'closures',
            'DeviceKernel': 'kernels', 'PassType': 'passes'}
COPYRIGHT = 'Copyright (c) 2026 Jeremy Ellis and contributors'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text())


def json_text(value):
    return json.dumps(value, indent=2, ensure_ascii=False) + '\n'


def uncomment(text):
    """Preserve source offsets/lines while removing comments, not string literals."""
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*'
    return re.sub(pattern, lambda m: re.sub(r'[^\n]', ' ', m[0])
                  if m[0].startswith('/') else m[0], text)


def enum_declarations(text, template):
    """Extract symbols only. Values always come from a compiled native program.

    Only the two known declaration macros are expanded here. New macro syntax
    fails until reviewed, and Clang independently checks the resulting membership.
    """
    text = uncomment(text)
    template = uncomment(template)
    declarations = []
    pattern = r'\benum\s+(?:(class)\s+)?(\w+)?\s*(?::\s*\w+\s*)?\{([^{}]*)\}'
    for match in re.finditer(pattern, text):
        body = match[3]
        if match[2] == 'ShaderNodeType':
            symbols = []
            for node in re.finditer(r'^SHADER_NODE_TYPE(_DERIVATIVE)?\((\w+)\)', template, re.M):
                symbols.append(node[2])
                if node[1]:
                    symbols.append(node[2] + '_DERIVATIVE')
            body = body.replace('#include "node_types_template.h"', ','.join(symbols) + ',')
        body = re.sub(r'^\s*#(?:[^\n]*\\\n)*[^\n]*', '', body, flags=re.M)
        body = re.sub(r'DECLARE_FILM_CONVERT_KERNEL\((\w+)\)',
                      lambda m: f'DEVICE_KERNEL_FILM_CONVERT_{m[1]},'
                      f'DEVICE_KERNEL_FILM_CONVERT_{m[1]}_HALF_RGBA', body)
        symbols = []
        for item in body.split(','):
            if not item.strip():
                continue
            parsed = re.fullmatch(r'\s*(\w+)\s*(?:=\s*([^,]+))?\s*', item)
            if not parsed:
                raise ValueError('Unreviewed enum declaration syntax: ' + item.strip())
            symbols.append(parsed[1])
        if not symbols:
            raise ValueError('Empty enum declaration')
        name = match[2] or 'anonymous:' + symbols[0]
        declarations.append(dict(name=name, symbols=symbols, scoped=bool(match[1]),
                                 line=text.count('\n', 0, match.start()) + 1))
    if len(declarations) != len(re.findall(r'\benum\b', text)):
        raise ValueError('Unreviewed enum syntax or forward declaration; refusing partial extraction')
    return declarations


def source_inventory():
    template = (SOURCE / 'src/kernel/svm/node_types_template.h').read_text()
    enums = []
    for header in HEADERS:
        for entry in enum_declarations((SOURCE / 'src' / header).read_text(), template):
            entry['source'] = 'third_party/cycles/src/' + header
            enums.append(entry)
    features = re.findall(r'^#define (KERNEL_FEATURE_\w+)\b',
                          (SOURCE / 'src/kernel/features.h').read_text(), re.M)
    tables = []
    for match in re.finditer(r'^KERNEL_DATA_ARRAY(_WRITABLE)?\((\w+), (\w+)\)',
                             (SOURCE / 'src/kernel/data_arrays.h').read_text(), re.M):
        tables.append(dict(symbol=match[3], type=match[2], writable=bool(match[1])))
    if not enums or not features or not tables:
        raise ValueError('Missing source declarations')
    return enums, features, tables


def ast_enum_sets(text):
    """Clang emits multiple JSON documents when the dump filter matches namespaces."""
    decoder, offset, result = json.JSONDecoder(), 0, {}

    def visit(node):
        if node.get('kind') == 'EnumDecl':
            symbols = [child['name'] for child in node.get('inner', [])
                       if child.get('kind') == 'EnumConstantDecl']
            if symbols:
                name = node.get('name') or 'anonymous:' + symbols[0]
                if name in result and result[name] != symbols:
                    raise ValueError('Ambiguous compiler enum: ' + name)
                result[name] = symbols
        for child in node.get('inner', []):
            visit(child)

    while offset < len(text):
        if text[offset].isspace():
            offset += 1
            continue
        node, offset = decoder.raw_decode(text, offset)
        visit(node)
    return result


def compare_declarations(enums, compiled):
    for entry in enums:
        actual = compiled.get(entry['name'])
        if actual != entry['symbols']:
            raise ValueError('Source/compiler enum mismatch: ' + entry['name'] +
                             f'\nsource={entry["symbols"]}\ncompiler={actual}')


def compile_metadata(enums, features, tables, work, cxx):
    work.mkdir(parents=True, exist_ok=True)
    source = work / 'metadata.cpp'
    lines = ['// ' + COPYRIGHT, '// SPDX-License-Identifier: Apache-2.0',
             '#include "kernel/types.h"', '#include "util/types_image.h"',
             '#include <iostream>', 'using namespace ccl;', 'int main() {']
    ids = []
    for entry in enums:
        for symbol in entry['symbols']:
            identifier = entry['name'] + ':' + symbol
            expression = entry['name'] + '::' + symbol if entry['scoped'] else symbol
            ids.append(identifier)
            lines.append(f'  std::cout << "{identifier}\\t" << static_cast<long long>({expression}) << "\\n";')
    for symbol in features:
        identifier = 'KernelFeature:' + symbol
        ids.append(identifier)
        lines.append(f'  std::cout << "{identifier}\\t" << static_cast<long long>({symbol}) << "\\n";')
    for table in tables:
        identifier = 'SceneTable:' + table['symbol']
        ids.append(identifier)
        lines.append(f'  std::cout << "{identifier}\\t" << sizeof({table["type"]}) << "\\n";')
    lines += ['  std::cout << "abi:pointerBytes\\t" << sizeof(void*) << "\\n";', '}']
    source.write_text('\n'.join(lines) + '\n')
    common = [cxx, '-std=c++20', '-DCCL_NAMESPACE_BEGIN=namespace ccl {',
              '-DCCL_NAMESPACE_END=}', '-I' + str(SOURCE / 'src'), str(source)]
    ast_path = work / 'metadata-ast.json'
    with ast_path.open('w') as output:
        subprocess.run(common + ['-fsyntax-only', '-Xclang', '-ast-dump=json',
                                 '-Xclang', '-ast-dump-filter=ccl'],
                       stdout=output, check=True)
    compare_declarations(enums, ast_enum_sets(ast_path.read_text()))
    binary = work / 'metadata'
    subprocess.run(common + ['-o', str(binary)], check=True)
    stdout = subprocess.check_output([str(binary)], text=True)
    (work / 'metadata.tsv').write_text(stdout)
    values = {}
    for line in stdout.splitlines():
        identifier, value = line.split('\t')
        if identifier in values:
            raise ValueError('Duplicate native metadata: ' + identifier)
        values[identifier] = int(value)
    if set(values) != set(ids) | {'abi:pointerBytes'}:
        raise ValueError('Incomplete native metadata')
    provenance = dict(compiler=subprocess.check_output([cxx, '--version'], text=True).splitlines()[0],
                      target=subprocess.check_output([cxx, '-dumpmachine'], text=True).strip(),
                      pointerBytes=values.pop('abi:pointerBytes'),
                      definitions=['CCL_NAMESPACE_BEGIN=namespace ccl {', 'CCL_NAMESPACE_END=}'],
                      profile='Native host headers, optional build features disabled; not a target ABI proof',
                      generatedSourceSha256=digest(source), binarySha256=digest(binary),
                      valuesSha256=hashlib.sha256(json_text(values).encode()).hexdigest())
    (work / 'compiler.json').write_text(json_text(provenance))
    return values, provenance


def verify_evidence(registry, root=ROOT):
    for name, evidence in registry.items():
        if not evidence['assertions'] or not evidence['sourceSha256']:
            raise ValueError('Evidence needs assertions and implementation hashes: ' + name)
        report = root / evidence['report']
        if digest(report) != evidence['sha256']:
            raise ValueError('Evidence report changed: ' + name)
        data = read_json(report)
        for assertion in evidence['assertions']:
            value = data
            for key in assertion['pointer'].strip('/').split('/'):
                value = value[int(key)] if isinstance(value, list) else value[key]
            if value != assertion['equals'] or type(value) is not type(assertion['equals']):
                raise ValueError('Evidence assertion failed: ' + name + ' ' + assertion['pointer'])
        for source, expected in evidence['sourceSha256'].items():
            if digest(root / source) != expected:
                raise ValueError('Evidence needs review after source change: ' + name + ' ' + source)


def work_item(family, enum_name, symbol):
    if family == 'kernels':
        if any(x in symbol for x in ('VOLUME',)):
            return 'P10-03'
        if any(x in symbol for x in ('SHADOW', 'LIGHT')):
            return 'P07-01'
        if any(x in symbol for x in ('SUBSURFACE', 'MNEE')):
            return 'P11-02'
        if any(x in symbol for x in ('FILM', 'FILTER', 'CRYPTOMATTE', 'ADAPTIVE')):
            return 'P12-01'
        return 'P02-11'
    if family == 'closures':
        return 'P06-01'
    if family == 'opcodes':
        return 'P05-01'
    if family == 'passes':
        return 'P12-01'
    if family == 'tables' or enum_name.startswith(('Image', 'Interpolation', 'Extension', 'Attribute')):
        return 'P04-01' if 'Attribute' in enum_name else 'P04-04'
    if enum_name.startswith('Node'):
        return 'P05-01'
    if any(x in enum_name for x in ('Camera', 'Panorama', 'MotionPosition', 'SamplingPattern', 'PathTraceDimension')):
        return 'P09-01'
    if 'Light' in enum_name:
        return 'P07-01'
    if any(x in enum_name for x in ('Primitive', 'Curve', 'BVH')):
        return 'P08-01'
    return 'P00-02'


def make_row(family, enum_name, symbol, source, value, reviews, evidence):
    identifier = enum_name + ':' + symbol
    row = dict(id=identifier, symbol=symbol, declaration=enum_name, source=source,
               classification='transport' if family == 'tables' else 'shader-behavior',
               workItems=[work_item(family, enum_name, symbol)], status='unassessed',
               coverage='unassessed', fullRequirementVerified=False,
               testStrategy={'opcodes': 'native-graph-and-typed-program',
                             'closures': 'setup-evaluate-sample-pdf-and-render',
                             'kernels': 'independent-cpu-state-and-native-scheduler',
                             'passes': 'raw-film-and-pass-metadata',
                             'tables': 'native-upload-layout-update-and-render'}.get(
                                 family, 'parameter-edge-cases-and-feature-combinations'),
               evidence=[])
    row['nativeElementBytes' if family == 'tables' else 'nativeValue'] = value
    if identifier in reviews:
        allowed = {'classification', 'workItems', 'status', 'coverage', 'evidence',
                   'notes', 'implementation', 'tests', 'testStrategy'}
        if set(reviews[identifier]) - allowed:
            raise ValueError('Review cannot override extracted identity/values: ' + identifier)
        row.update(reviews[identifier])
    row['classificationReviewed'] = 'classification' in reviews.get(identifier, {})
    if row['fullRequirementVerified']:
        raise ValueError('This initial inventory cannot certify full requirements: ' + identifier)
    if row['status'] != 'unassessed' and (row['coverage'] != 'partial' or not row['evidence']):
        raise ValueError('Tested status requires bounded evidence: ' + identifier)
    if row['status'] not in ('unassessed', 'isolated-tested', 'integrated-tested', 'platform-tested'):
        raise ValueError('Unreviewed status: ' + identifier)
    if row['coverage'] not in ('unassessed', 'partial'):
        raise ValueError('Invalid coverage: ' + identifier)
    if any(name not in evidence for name in row['evidence']):
        raise ValueError('Unknown evidence reference: ' + identifier)
    for path in row.get('implementation', []) + row.get('tests', []):
        if not (ROOT / path).is_file():
            raise ValueError('Missing implementation/test: ' + path)
    return row


def platforms(evidence):
    gates = ('compile', 'link', 'load', 'adapterDiscovery', 'basicCompute', 'scenePreparation',
             'render', 'numericalCorpus', 'stress', 'packageInstall', 'licenseClearance', 'releaseSignoff')
    # Adapter discovery and basic compute are WebGPU runtime stages; Cycles'
    # Metal device performs its own device setup.
    metal_gates = tuple(gate for gate in gates if gate not in ('adapterDiscovery', 'basicCompute'))
    rows = []
    for rid in RIDS:
        metal = rid in METAL_RIDS
        row = dict(rid=rid, required=True, gpuBackend='metal' if metal else 'webgpu',
                   workItems=['P15-C01', 'P15-C02', 'P15-C05', 'P18-09'],
                   nativeHost={gate: 'pending' for gate in (metal_gates if metal else gates)},
                   replayEvidence=[], nativeHostEvidence=[], fullRequirementVerified=False)
        if metal:
            row['statusRecord'] = METAL_STATUS
        if rid.startswith('android'):
            row['minimumApi'] = 33
        if rid == 'linux-x64':
            for gate in gates[:8]:
                row['nativeHost'][gate] = 'bounded-proof'
            row['nativeHostEvidence'] = ['native-render']
        for name, item in evidence.items():
            if item.get('kind') == 'command-replay' and item['rid'] == rid:
                row['replayEvidence'].append(name)
        rows.append(row)
    return rows


def generate(values, enums, features, tables, reviews, evidence):
    documents = {family: [] for family in ('opcodes', 'closures', 'kernels', 'passes', 'modes', 'features', 'tables')}
    for entry in enums:
        family = FAMILIES.get(entry['name'], 'modes')
        for symbol in entry['symbols']:
            documents[family].append(make_row(family, entry['name'], symbol, entry['source'],
                                             values[entry['name'] + ':' + symbol], reviews, evidence))
    for symbol in features:
        documents['features'].append(make_row('features', 'KernelFeature', symbol,
                                              'third_party/cycles/src/kernel/features.h',
                                              values['KernelFeature:' + symbol], reviews, evidence))
    for table in tables:
        row = make_row('tables', 'SceneTable', table['symbol'], 'third_party/cycles/src/kernel/data_arrays.h',
                       values['SceneTable:' + table['symbol']], reviews, evidence)
        row.update(nativeType=table['type'], writable=table['writable'])
        documents['tables'].append(row)
    known = {row['id'] for rows in documents.values() for row in rows}
    if set(reviews) - known:
        raise ValueError('Review references removed/renamed symbols: ' + str(sorted(set(reviews) - known)))
    documents['platforms'] = platforms(evidence)
    documents['dependencies'] = []
    for component in read_json(ROOT / 'third_party/dependency-imports.json')['components']:
        documents['dependencies'].append({
            **{key: value for key, value in component.items() if key not in ('files', 'excluded')},
            'workItems': ['P13-01', 'P17-18'], 'status': 'source-reviewed',
            'sourceManifest': 'third_party/dependency-imports.json',
            'allRidValidation': 'pending', 'fullRequirementVerified': False})
    for name in ('embree', 'oidn', 'osl', 'llvm', 'openvdb', 'nanovdb', 'opensubdiv', 'openpgl'):
        documents['dependencies'].append(dict(id=name, status='unassessed',
                                              workItems=['P13-01', 'P13-05'],
                                              sourceManifest=None, allRidValidation='pending',
                                              fullRequirementVerified=False))
    result = {}
    for family, rows in documents.items():
        result[family + '.json'] = dict(schemaVersion=1, copyright=COPYRIGHT,
                                       cyclesRevision=REVISION, inventoryComplete=False,
                                       fullCyclesParity=False, rows=rows)
    return result


def write_or_check(outputs, directory, check):
    mismatches = []
    for filename, value in outputs.items():
        path, content = directory / filename, json_text(value)
        if not check:
            path.write_text(content)
        elif not path.exists() or path.read_text() != content:
            mismatches.append(filename)
    if mismatches:
        raise ValueError('Compatibility inventory is stale: ' + ', '.join(mismatches))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('generate', 'check'))
    parser.add_argument('--cxx', default='clang++', help='Host Clang compiler, required for independent AST check')
    parser.add_argument('--work', type=Path, default=ROOT / 'native/work/compatibility')
    args = parser.parse_args()
    verify_cycles()
    baseline = read_json(DIRECTORY / 'baseline.json')
    if baseline['cyclesRevision'] != REVISION:
        raise ValueError('Compatibility baseline differs from reviewed Cycles revision')
    expected_inputs = {'third_party/cycles/src/' + name for name in INPUTS}
    if set(baseline['sourceSha256']) != expected_inputs:
        raise ValueError('Baseline declaration inputs require review')
    for path, expected in baseline['sourceSha256'].items():
        if digest(ROOT / path) != expected:
            raise ValueError('Declaration source changed; review baseline and removed requirements: ' + path)
    evidence = read_json(DIRECTORY / 'evidence.json')['checkpoints']
    verify_evidence(evidence)
    reviews = read_json(DIRECTORY / 'reviews.json')['symbols']
    enums, features, tables = source_inventory()
    values, compiler = compile_metadata(enums, features, tables, args.work.resolve(), args.cxx)
    if any(compiler[key] != value for key, value in baseline['nativeMetadataProfile'].items()):
        raise ValueError('Host metadata profile changed; record separate target metadata instead of replacing the baseline')
    outputs = generate(values, enums, features, tables, reviews, evidence)
    for document in outputs.values():
        document['nativeMetadataProfile'] = baseline['nativeMetadataProfile']
    write_or_check(outputs, DIRECTORY, args.action == 'check')
    result = dict(passed=True, cyclesRevision=REVISION, inventoryComplete=False, fullCyclesParity=False,
                  counts={name[:-5]: len(value['rows']) for name, value in outputs.items()},
                  declarationEnums=len(enums), compiler=compiler,
                  evidenceReportsChecked=len(evidence),
                  inventorySha256={name: digest(DIRECTORY / name) for name in outputs},
                  nextWork=baseline['remainingInventoryWork'])
    (args.work / 'validation.json').write_text(json_text(result))
    print(json_text({key: value for key, value in result.items()
                     if key not in ('inventorySha256', 'compiler', 'nextWork')}))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
