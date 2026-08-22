"""Recognize and edit Kearne-generated Python without executing it."""

from __future__ import annotations

import ast
import io
import re
import tokenize
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from typing import Literal, TypeAlias

from blake3 import blake3

from kearne._sketch_schema import (
    CONSTRAINT_HELPERS,
    ENTITY_HELPERS,
    HELPERS,
    OBJECT_HELPERS,
    HelperSpec,
)

Section: TypeAlias = Literal["objects", "entities", "constraints"]
OWNED_NAMES = frozenset({"SketchDefinition", *HELPERS})
UNIT_NAMES = frozenset({"m", "mm", "inch", "rad", "deg"})
DIRECT_HELPER_BINDINGS: Mapping[str, str] = {name: name for name in HELPERS}
MAXIMUM_SOURCE_EDIT_BATCH = 65_536
_UUID7 = re.compile(
    r"[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}"
).fullmatch


class _Unrecognized(Exception):
    pass


class SourceError(ValueError):
    """A source operation could not preserve the recognized contract."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True, slots=True)
class SourceSpan:
    start_line: int
    start_column: int
    end_line: int
    end_column: int


@dataclass(frozen=True, slots=True)
class RecognizedCall:
    id: str
    kind: str
    code: str
    span: SourceSpan


@dataclass(frozen=True, slots=True)
class Recognition:
    function: str
    plane_code: str
    objects: tuple[RecognizedCall, ...]
    entities: tuple[RecognizedCall, ...]
    constraints: tuple[RecognizedCall, ...]
    source_digest: str


@dataclass(frozen=True, slots=True)
class AppendCall:
    section: Section
    call: str


@dataclass(frozen=True, slots=True)
class ReplaceCall:
    section: Section
    id: str
    call: str


@dataclass(frozen=True, slots=True)
class DeleteCall:
    section: Section
    id: str


SourceEdit: TypeAlias = AppendCall | ReplaceCall | DeleteCall


@dataclass(frozen=True, slots=True)
class SourceEditResult:
    source: str
    prior_digest: str
    source_digest: str
    recognition: Recognition


@dataclass(frozen=True, slots=True)
class _Span:
    start: int
    end: int


@dataclass(frozen=True, slots=True)
class _Source:
    data: bytes
    line_starts: tuple[int, ...]
    ascii: bool

    @staticmethod
    def create(text: str) -> _Source:
        try:
            data = text.encode()
        except UnicodeEncodeError as error:
            raise SourceError(
                "source.python.encoding", "source is not UTF-8"
            ) from error
        return _Source.from_data(data)

    @staticmethod
    def from_data(data: bytes) -> _Source:
        starts = [0]
        for line in data.splitlines(keepends=True):
            starts.append(starts[-1] + len(line))
        return _Source(data, tuple(starts), data.isascii())

    def point(self, line: int, column_bytes: int) -> int:
        return self.line_starts[line - 1] + column_bytes

    def span(self, node: ast.AST) -> _Span:
        line = getattr(node, "lineno", None)
        column = getattr(node, "col_offset", None)
        end_line = getattr(node, "end_lineno", None)
        end_column = getattr(node, "end_col_offset", None)
        if line is None or column is None or end_line is None or end_column is None:
            raise SourceError("source.edit.internal", "syntax node has no source span")
        return _Span(self.point(line, column), self.point(end_line, end_column))

    def code(self, node: ast.AST) -> str:
        span = self.span(node)
        return self.data[span.start : span.end].decode()

    def public_span(self, node: ast.expr, span: _Span) -> SourceSpan:
        start_line = node.lineno
        end_line = node.end_lineno
        end_column = node.end_col_offset
        if end_line is None or end_column is None:
            raise SourceError("source.edit.internal", "syntax node has no end line")
        return SourceSpan(
            start_line,
            node.col_offset if self.ascii else self.column(start_line, span.start),
            end_line,
            end_column if self.ascii else self.column(end_line, span.end),
        )

    def column(self, line: int, offset: int) -> int:
        start = self.line_starts[line - 1]
        return len(self.data[start:offset].decode())


@dataclass(frozen=True, slots=True)
class _ParsedCall:
    id: str
    kind: str
    public: RecognizedCall | None
    span: _Span
    spec: HelperSpec
    references: tuple[tuple[str, frozenset[str]], ...]
    entity_references: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class _ParsedSection:
    kind: Literal["tuple", "list"]
    span: _Span
    close: int


@dataclass(frozen=True, slots=True)
class _ParsedRecognition:
    public: Recognition | None
    source: _Source
    objects_section: _ParsedSection | None
    entities_section: _ParsedSection
    constraints_section: _ParsedSection
    objects: tuple[_ParsedCall, ...]
    entities: tuple[_ParsedCall, ...]
    constraints: tuple[_ParsedCall, ...]
    bindings: Mapping[str, str]


@dataclass(frozen=True, slots=True)
class SourceEditSession:
    """One explicitly owned, recognized source revision."""

    function: str
    source_digest: str
    recognition: Recognition
    _parsed: _ParsedRecognition = field(repr=False, compare=False)

    @property
    def source(self) -> str:
        """Return this revision's source without retaining a second copy."""
        return self._parsed.source.data.decode()

    def apply(
        self, expected_digest: str, edits: Sequence[SourceEdit]
    ) -> SourceEditSession:
        """Atomically publish one validated batch as a new session."""
        if expected_digest != self.source_digest:
            raise SourceError(
                "source.edit.stale", "source changed after it was observed"
            )
        _validate_batch_size(len(edits))
        updated = _apply_parsed_edits(self._parsed, self.function, tuple(edits))
        return _session(updated)


def source_digest(source: str) -> str:
    """Return the revision precondition digest for UTF-8 Python source."""
    try:
        encoded = source.encode()
    except UnicodeEncodeError as error:
        raise SourceError("source.python.encoding", "source is not UTF-8") from error
    return _source_digest_bytes(encoded)


def _source_digest_bytes(source: bytes) -> str:
    digest = blake3(source, derive_key_context="kearne.content.blob.v1").hexdigest()
    return f"blake3:{digest}"


def _call_name(call: ast.Call) -> str | None:
    return call.func.id if isinstance(call.func, ast.Name) else None


class _BoundNameVisitor(ast.NodeVisitor):
    def __init__(self) -> None:
        self.names: set[str] = set()

    def visit_Name(self, node: ast.Name) -> None:
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            self.names.add(node.id)

    def visit_Import(self, node: ast.Import) -> None:
        self.names.update(
            alias.asname or alias.name.split(".")[0] for alias in node.names
        )

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        self.names.update(
            alias.asname or alias.name for alias in node.names if alias.name != "*"
        )

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        self.names.add(node.name)

    def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:
        self.names.add(node.name)

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        self.names.add(node.name)

    def visit_Lambda(self, node: ast.Lambda) -> None:
        return

    def visit_Global(self, node: ast.Global) -> None:
        self.names.update(node.names)

    def visit_Nonlocal(self, node: ast.Nonlocal) -> None:
        self.names.update(node.names)

    def visit_ExceptHandler(self, node: ast.ExceptHandler) -> None:
        if node.name is not None:
            self.names.add(node.name)
        self.generic_visit(node)

    def visit_MatchAs(self, node: ast.MatchAs) -> None:
        if node.name is not None:
            self.names.add(node.name)
        self.generic_visit(node)

    def visit_MatchStar(self, node: ast.MatchStar) -> None:
        if node.name is not None:
            self.names.add(node.name)


def _bound_names(statements: list[ast.stmt]) -> set[str]:
    visitor = _BoundNameVisitor()
    for statement in statements:
        visitor.visit(statement)
    return visitor.names


def _target_names(target: ast.expr) -> set[str]:
    return {
        node.id
        for node in ast.walk(target)
        if isinstance(node, ast.Name) and isinstance(node.ctx, (ast.Store, ast.Del))
    }


def _statement_bound_names(statement: ast.stmt) -> set[str]:
    if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
        return {statement.name}
    if isinstance(statement, (ast.Import, ast.ImportFrom)):
        return _bound_names([statement])
    if isinstance(statement, (ast.Global, ast.Nonlocal)):
        return set(statement.names)
    if isinstance(statement, (ast.Assign, ast.Delete)):
        return set().union(*(_target_names(target) for target in statement.targets))
    if isinstance(statement, (ast.AnnAssign, ast.AugAssign)):
        return _target_names(statement.target)
    if isinstance(statement, (ast.For, ast.AsyncFor)):
        names = _target_names(statement.target)
        nested = (*statement.body, *statement.orelse)
    elif isinstance(statement, (ast.With, ast.AsyncWith)):
        names = set().union(
            *(
                _target_names(item.optional_vars)
                for item in statement.items
                if item.optional_vars is not None
            )
        )
        nested = tuple(statement.body)
    elif isinstance(statement, ast.If | ast.While):
        names = set()
        nested = (*statement.body, *statement.orelse)
    elif isinstance(statement, (ast.Try, ast.TryStar)):
        names = {
            handler.name for handler in statement.handlers if handler.name is not None
        }
        nested = (
            *statement.body,
            *(child for handler in statement.handlers for child in handler.body),
            *statement.orelse,
            *statement.finalbody,
        )
    elif isinstance(statement, ast.Match):
        names = set()
        for case in statement.cases:
            visitor = _BoundNameVisitor()
            visitor.visit(case.pattern)
            names.update(visitor.names)
        nested = tuple(child for case in statement.cases for child in case.body)
    else:
        return set()
    for child in nested:
        names.update(_statement_bound_names(child))
    return names


def _function_bound_names(
    function: ast.FunctionDef, *, contains_named_expression: bool
) -> set[str]:
    arguments = function.args
    names = {
        argument.arg
        for argument in (*arguments.posonlyargs, *arguments.args, *arguments.kwonlyargs)
    }
    if arguments.vararg is not None:
        names.add(arguments.vararg.arg)
    if arguments.kwarg is not None:
        names.add(arguments.kwarg.arg)
    if contains_named_expression:
        return names | _bound_names(function.body)
    for statement in function.body:
        names.update(_statement_bound_names(statement))
    return names


def _owned_imports(module: ast.Module) -> Mapping[str, str]:
    owned: dict[str, str] = {}
    seen: set[str] = set()
    ambiguous: set[str] = set()
    for statement in module.body:
        if isinstance(statement, ast.ImportFrom) and any(
            alias.name == "*" for alias in statement.names
        ):
            return {}
        sketch_import = (
            isinstance(statement, ast.ImportFrom)
            and statement.level == 0
            and statement.module == "kearne.sketch"
        )
        unit_import = (
            isinstance(statement, ast.ImportFrom)
            and statement.level == 0
            and statement.module == "kearne.units"
        )
        if sketch_import or unit_import:
            if not isinstance(statement, ast.ImportFrom):
                return {}
            permitted = OWNED_NAMES if sketch_import else UNIT_NAMES
            names = {
                alias.asname or alias.name: alias.name
                for alias in statement.names
                if alias.name in permitted
            }
            other = {
                alias.asname or alias.name
                for alias in statement.names
                if alias.name not in permitted
            }
        else:
            names = {}
            other = _bound_names([statement])
        for local, canonical in names.items():
            if local in seen:
                ambiguous.add(local)
                owned.pop(local, None)
            elif local not in ambiguous:
                owned[local] = canonical
            seen.add(local)
        for local in other:
            if local in seen:
                ambiguous.add(local)
                owned.pop(local, None)
            seen.add(local)
    return owned


def _owned_call(call: ast.Call, bindings: Mapping[str, str]) -> str | None:
    name = _call_name(call)
    return bindings.get(name) if name is not None else None


def _literal_string(node: ast.expr, code: str) -> str:
    if not isinstance(node, ast.Constant):
        raise _Unrecognized
    if not isinstance(node.value, str):
        raise SourceError(code, "stable ID must be a string literal")
    return node.value


def _stable_id(node: ast.expr, code: str) -> str:
    value = _literal_string(node, code)
    if _UUID7(value) is None:
        raise SourceError(code, "stable ID is not a canonical UUIDv7")
    return value


def _validate_keywords(call: ast.Call, spec: HelperSpec, code: str) -> None:
    if not call.keywords:
        return
    expected = {keyword.name: keyword for keyword in spec.keywords}
    seen: set[str] = set()
    for keyword in call.keywords:
        if keyword.arg is None or keyword.arg not in expected or keyword.arg in seen:
            raise SourceError(code, "helper keywords are invalid")
        seen.add(keyword.arg)
        keyword_spec = expected[keyword.arg]
        if keyword_spec.kind == "boolean":
            if not isinstance(keyword.value, ast.Constant):
                raise _Unrecognized
            if not isinstance(keyword.value.value, bool):
                raise SourceError(code, "helper keyword value is invalid")
        elif keyword_spec.kind == "enum":
            allowed = keyword_spec.values
            if not isinstance(keyword.value, ast.Constant):
                raise _Unrecognized
            if not isinstance(keyword.value.value, str):
                raise SourceError(code, "helper keyword value is invalid")
            if keyword.value.value not in allowed:
                raise SourceError(code, "helper keyword value is invalid")
        else:
            if not isinstance(keyword.value, ast.Constant):
                raise _Unrecognized
            if not isinstance(keyword.value.value, str):
                raise SourceError(code, "helper keyword value is invalid")


def _may_contain_identifier(source: bytes, names: tuple[str, ...]) -> bool:
    if not names:
        return False
    alternatives = b"|".join(name.encode() for name in names)
    return re.search(rb"(?a:\b(?:" + alternatives + rb")\b)", source) is not None


def _validate_owned_units(
    node: ast.AST, bindings: Mapping[str, str], source: bytes | None = None
) -> None:
    unsafe = tuple(name for name in UNIT_NAMES if bindings.get(name) != name)
    if source is not None and not _may_contain_identifier(source, unsafe):
        return
    pending = [node]
    while pending:
        nested = pending.pop()
        if isinstance(nested, ast.Call):
            pending.extend(nested.args)
            pending.extend(keyword.value for keyword in nested.keywords)
            if not isinstance(nested.func, ast.Name):
                pending.append(nested.func)
                continue
        elif isinstance(nested, (ast.Tuple, ast.List, ast.Set)):
            pending.extend(nested.elts)
            continue
        elif isinstance(nested, ast.Dict):
            pending.extend(value for value in nested.keys if value is not None)
            pending.extend(nested.values)
            continue
        elif isinstance(nested, ast.BinOp):
            pending.extend((nested.left, nested.right))
            continue
        elif isinstance(nested, ast.UnaryOp):
            pending.append(nested.operand)
            continue
        elif isinstance(nested, (ast.Constant, ast.Name)):
            continue
        else:
            pending.extend(ast.iter_child_nodes(nested))
            continue
        local = nested.func.id
        if local in unsafe:
            raise _Unrecognized


def _validate_call(
    call: ast.Call,
    spec: HelperSpec,
    bindings: Mapping[str, str],
    code: str,
) -> tuple[str, tuple[tuple[str, frozenset[str]], ...], tuple[str, ...]]:
    if len(call.args) != len(spec.positional):
        raise SourceError(code, "helper positional argument count is invalid")
    _validate_keywords(call, spec, code)
    declared = ""
    references: list[tuple[str, frozenset[str]]] = []
    entity_references: list[str] = []
    for node, argument in zip(call.args, spec.positional, strict=True):
        if argument.kind == "stable_id":
            declared = _stable_id(node, code)
        elif argument.kind == "label":
            label = _literal_string(node, code)
            if (
                not label.strip()
                or len(label.encode()) > 128
                or any(
                    ord(character) < 0x20 or ord(character) == 0x7F
                    for character in label
                )
            ):
                raise SourceError(code, "Sketch object label is invalid")
        elif argument.kind == "entity_ref":
            reference = _stable_id(node, code)
            references.append((reference, argument.entity_kinds))
            entity_references.append(reference)
        elif argument.kind == "point_ref":
            if not isinstance(node, ast.Call):
                raise _Unrecognized
            reference_name = _owned_call(node, bindings)
            reference_spec = HELPERS.get(reference_name or "")
            if reference_spec is None or reference_spec.section != "references":
                raise _Unrecognized
            _, nested, _ = _validate_call(node, reference_spec, bindings, code)
            references.extend(nested)
    return declared, tuple(references), tuple(entity_references)


def _find_function(module: ast.Module, qualified_name: str) -> ast.FunctionDef | None:
    found: list[ast.FunctionDef] = []

    def visit(statements: list[ast.stmt], scope: tuple[str, ...]) -> None:
        for statement in statements:
            if isinstance(statement, ast.ClassDef):
                visit(statement.body, (*scope, statement.name))
            elif (
                isinstance(statement, ast.FunctionDef)
                and ".".join((*scope, statement.name)) == qualified_name
            ):
                found.append(statement)

    visit(module.body, ())
    if len(found) > 1:
        raise SourceError("source.function.ambiguous", "function name is ambiguous")
    return found[0] if found else None


def _returned_definition(
    function: ast.FunctionDef, bindings: Mapping[str, str]
) -> ast.Call | None:
    if (
        function.decorator_list
        or not function.body
        or not isinstance(function.body[-1], ast.Return)
    ):
        return None
    returned = function.body[-1].value
    if not isinstance(returned, ast.Call) or returned.args or returned.keywords:
        return None
    if not isinstance(returned.func, ast.Attribute) or returned.func.attr != "build":
        return None
    definition = returned.func.value
    if (
        not isinstance(definition, ast.Call)
        or _owned_call(definition, bindings) != "SketchDefinition"
    ):
        return None
    return definition


def _keywords(definition: ast.Call) -> dict[str, ast.expr]:
    if definition.args:
        raise _Unrecognized
    result: dict[str, ast.expr] = {}
    for keyword in definition.keywords:
        if keyword.arg is None or keyword.arg in result:
            raise _Unrecognized
        result[keyword.arg] = keyword.value
    if set(result) not in (
        {"plane", "entities", "constraints"},
        {"plane", "objects", "entities", "constraints"},
    ):
        raise _Unrecognized
    return result


def _elements(
    source: _Source, node: ast.expr
) -> tuple[_ParsedSection, tuple[ast.Call, ...]]:
    if isinstance(node, ast.Tuple):
        kind: Literal["tuple", "list"] = "tuple"
        delimiters = b"()"
    elif isinstance(node, ast.List):
        kind = "list"
        delimiters = b"[]"
    else:
        raise _Unrecognized
    span = source.span(node)
    if (
        source.data[span.start : span.start + 1] != delimiters[:1]
        or source.data[span.end - 1 : span.end] != delimiters[1:]
    ):
        raise _Unrecognized
    calls: list[ast.Call] = []
    for element in node.elts:
        if not isinstance(element, ast.Call):
            raise _Unrecognized
        calls.append(element)
    return _ParsedSection(kind, span, span.end - 1), tuple(calls)


def _parse(
    source_text: str,
    function: str,
    *,
    units_already_validated: bool = False,
    materialize_public: bool = True,
    prepared_source: _Source | None = None,
) -> _ParsedRecognition | None:
    source = prepared_source or _Source.create(source_text)
    try:
        module = ast.parse(source_text)
    except SyntaxError as error:
        raise SourceError("source.python.syntax", "Python source is invalid") from error
    found = _find_function(module, function)
    if found is None:
        return None
    bindings = _owned_imports(module)
    if (
        _function_bound_names(found, contains_named_expression=":=" in source_text)
        & bindings.keys()
    ):
        return None
    if not units_already_validated:
        try:
            function_span = source.span(found)
            _validate_owned_units(
                found, bindings, source.data[function_span.start : function_span.end]
            )
        except _Unrecognized:
            return None
    definition = _returned_definition(found, bindings)
    if definition is None:
        return None
    try:
        keywords = _keywords(definition)
        if "objects" in keywords:
            objects_section, object_nodes = _elements(source, keywords["objects"])
        else:
            objects_section, object_nodes = None, ()
        entities_section, entity_nodes = _elements(source, keywords["entities"])
        constraints_section, constraint_nodes = _elements(
            source, keywords["constraints"]
        )
    except _Unrecognized:
        return None

    def parsed_calls(
        nodes: tuple[ast.Call, ...], allowed: frozenset[str], section: Section
    ) -> tuple[_ParsedCall, ...]:
        result: list[_ParsedCall] = []
        seen: set[str] = set()
        for node in nodes:
            kind = _owned_call(node, bindings)
            if kind is None:
                raise _Unrecognized
            spec = HELPERS.get(kind)
            if spec is None or kind not in allowed:
                raise SourceError(
                    "source.sketch.unknown-helper",
                    f"{section} helper is not recognized",
                )
            stable, references, entity_references = _validate_call(
                node, spec, bindings, "source.sketch.invalid-helper-call"
            )
            if stable in seen:
                raise SourceError(
                    "source.sketch.duplicate-stable-id",
                    f"{section} stable ID is duplicated",
                )
            seen.add(stable)
            span = source.span(node)
            result.append(
                _ParsedCall(
                    stable,
                    kind,
                    RecognizedCall(
                        stable,
                        kind,
                        source.data[span.start : span.end].decode(),
                        source.public_span(node, span),
                    )
                    if materialize_public
                    else None,
                    span,
                    spec,
                    references,
                    entity_references,
                )
            )
        return tuple(result)

    try:
        objects = parsed_calls(object_nodes, OBJECT_HELPERS, "objects")
        entities = parsed_calls(entity_nodes, ENTITY_HELPERS, "entities")
        constraints = parsed_calls(constraint_nodes, CONSTRAINT_HELPERS, "constraints")
    except _Unrecognized:
        return None
    entity_kinds = {entry.id: entry.spec.entity_kind for entry in entities}
    for entry in (*objects, *constraints):
        if any(reference not in entity_kinds for reference, _ in entry.references):
            raise SourceError(
                "source.sketch.missing-entity", "constraint references a missing entity"
            )
        if any(
            entity_kinds[reference] not in allowed
            for reference, allowed in entry.references
        ):
            raise SourceError(
                "source.sketch.incompatible-entity",
                "constraint reference has an incompatible entity kind",
            )
        referenced_kinds = tuple(
            entity_kinds[reference] for reference in entry.entity_references
        )
        if entry.spec.entity_combinations and not any(
            all(
                kind in allowed
                for kind, allowed in zip(referenced_kinds, combination, strict=True)
            )
            for combination in entry.spec.entity_combinations
        ):
            raise SourceError(
                "source.sketch.incompatible-entity-combination",
                "constraint entity combination is invalid",
            )

    public: Recognition | None = None
    if materialize_public:
        public = Recognition(
            function,
            source.code(keywords["plane"]),
            tuple(entry.public for entry in objects if entry.public is not None),
            tuple(entry.public for entry in entities if entry.public is not None),
            tuple(entry.public for entry in constraints if entry.public is not None),
            _source_digest_bytes(source.data),
        )
    return _ParsedRecognition(
        public,
        source,
        objects_section,
        entities_section,
        constraints_section,
        objects,
        entities,
        constraints,
        bindings,
    )


def recognize(source: str, function: str) -> Recognition | None:
    """Recognize a generated sketch without importing or executing source."""
    parsed = _parse(source, function)
    return None if parsed is None else parsed.public


def _replacement(
    call: str, section: Section, bindings: Mapping[str, str]
) -> tuple[bytes, str]:
    raw = call.strip().encode()
    try:
        expression = ast.parse(raw.decode(), mode="eval").body
    except (SyntaxError, UnicodeDecodeError) as error:
        raise SourceError(
            "source.edit.invalid-call", "replacement is invalid"
        ) from error
    if not isinstance(expression, ast.Call):
        raise SourceError("source.edit.invalid-call", "replacement is not one call")
    source = _Source.create(raw.decode())
    span = source.span(expression)
    if span != _Span(0, len(raw)):
        raise SourceError("source.edit.invalid-call", "replacement is not one call")
    allowed = {
        "objects": OBJECT_HELPERS,
        "entities": ENTITY_HELPERS,
        "constraints": CONSTRAINT_HELPERS,
    }[section]
    name = _call_name(expression)
    spec = HELPERS.get(name or "")
    if spec is None or name not in allowed:
        raise SourceError(
            "source.edit.wrong-section", "replacement helper belongs to another section"
        )
    try:
        replacement_bindings = {**bindings, **DIRECT_HELPER_BINDINGS}
        _validate_owned_units(expression, replacement_bindings, raw)
        stable, _, _ = _validate_call(
            expression, spec, replacement_bindings, "source.edit.invalid-call"
        )
    except _Unrecognized as error:
        raise SourceError(
            "source.edit.invalid-call", "replacement is not statically editable"
        ) from error
    return raw, stable


def _direct_comma(source: bytes, lower: int, upper: int) -> _Span | None:
    text = source[lower:upper].decode()
    lines = tuple(text.splitlines(keepends=True))

    def offset(position: tuple[int, int]) -> int:
        line, column = position
        return lower + len(
            "".join((*lines[: line - 1], lines[line - 1][:column])).encode()
        )

    try:
        tokens = tokenize.generate_tokens(io.StringIO(text).readline)
        for token in tokens:
            if token.type == tokenize.OP and token.string == ",":
                return _Span(offset(token.start), offset(token.end))
    except (IndentationError, tokenize.TokenError) as error:
        raise SourceError("source.python.syntax", "Python source is invalid") from error
    return None


def _line_start(source: bytes, offset: int) -> int:
    return source.rfind(b"\n", 0, offset) + 1


def _line_end(source: bytes, offset: int) -> int:
    newline = source.find(b"\n", offset)
    return len(source) if newline < 0 else newline + 1


def _line_prefix(source: bytes, offset: int) -> bytes | None:
    start = _line_start(source, offset)
    prefix = source[start:offset]
    return prefix if not prefix.strip() else None


def _indented_continuations(call: bytes, indent: bytes, newline: bytes) -> bytes:
    normalized = call.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return normalized.replace(b"\n", newline + indent)


_Splice: TypeAlias = tuple[int, int, bytes]


def _apply_bytes(source: bytes, edits: Sequence[_Splice]) -> bytes:
    result = source
    for start, end, replacement in sorted(edits, reverse=True):
        result = result[:start] + replacement + result[end:]
    return result


def _append_edits(
    source: bytes,
    section: _ParsedSection,
    spans: Sequence[_Span],
    call: bytes,
) -> tuple[list[_Splice], _Span]:
    close_line = _line_start(source, section.close)
    close_indent = _line_prefix(source, section.close)
    multiline = close_line > section.span.start and close_indent is not None
    last = spans[-1] if spans else None
    trailing = (
        _direct_comma(source, last.end, section.close) if last is not None else None
    )
    edits: list[_Splice] = []
    if multiline and close_indent is not None:
        if last is not None and trailing is None:
            edits.append((last.end, last.end, b","))
        indent = close_indent + b"    "
        if spans:
            existing_indent = _line_prefix(source, spans[0].start)
            if existing_indent is not None:
                indent = existing_indent
        newline = (
            b"\r\n"
            if close_line >= 2 and source[close_line - 2 : close_line] == b"\r\n"
            else b"\n"
        )
        formatted = _indented_continuations(call, indent, newline)
        insertion = (close_line, close_line, indent + formatted + b"," + newline)
        call_start = close_line + sum(
            len(replacement) - (end - start)
            for start, end, replacement in edits
            if end <= close_line
        )
        edits.append(insertion)
        call_start += len(indent)
        return edits, _Span(call_start, call_start + len(formatted))

    if not spans:
        prefix = b""
        inserted = call + (b"," if section.kind == "tuple" else b"")
    elif trailing is None:
        prefix = b", "
        inserted = prefix + call
    else:
        prefix = b" "
        inserted = prefix + call + b","
    start = section.close + len(prefix)
    return [(section.close, section.close, inserted)], _Span(start, start + len(call))


def _delete_edits(
    source: bytes,
    section: _ParsedSection,
    spans: Sequence[_Span],
    target_index: int,
) -> list[_Splice]:
    target = spans[target_index]
    previous = spans[target_index - 1] if target_index else None
    following = spans[target_index + 1] if target_index + 1 < len(spans) else None
    target_line = _line_start(source, target.start)
    target_indent = _line_prefix(source, target.start)
    if following is not None:
        following_line = _line_start(source, following.start)
        if target_indent is not None and following_line > target_line:
            edits = [(target_line, _line_end(source, target.end), b"")]
            if (
                section.kind == "tuple"
                and len(spans) == 2
                and _direct_comma(source, following.end, section.close) is None
            ):
                edits.append((following.end, following.end, b","))
            return edits
    else:
        close_line = _line_start(source, section.close)
        if target_indent is not None and close_line > target_line:
            return [(target_line, _line_end(source, target.end), b"")]

    next_bound = following.start if following is not None else section.close
    trailing = _direct_comma(source, target.end, next_bound)
    if following is not None:
        edits = [(target.start, following.start, b"")]
        if (
            section.kind == "tuple"
            and len(spans) == 2
            and _direct_comma(source, following.end, section.close) is None
        ):
            edits.append((following.end, following.end, b","))
        return edits

    if previous is None:
        end = trailing.end if trailing is not None else target.end
        return [(target.start, end, b"")]
    separator = _direct_comma(source, previous.end, target.start)
    start = separator.end if separator is not None else target.start
    end = trailing.end if trailing is not None else target.end
    return [(start, end, b"")]


@dataclass(slots=True)
class _BatchEntry:
    id: str
    span: _Span


@dataclass(slots=True)
class _BatchState:
    source: bytes
    objects_section: _ParsedSection | None
    entities_section: _ParsedSection
    constraints_section: _ParsedSection
    objects: list[_BatchEntry]
    entities: list[_BatchEntry]
    constraints: list[_BatchEntry]

    @staticmethod
    def create(parsed: _ParsedRecognition) -> _BatchState:
        return _BatchState(
            parsed.source.data,
            parsed.objects_section,
            parsed.entities_section,
            parsed.constraints_section,
            [_BatchEntry(entry.id, entry.span) for entry in parsed.objects],
            [_BatchEntry(entry.id, entry.span) for entry in parsed.entities],
            [_BatchEntry(entry.id, entry.span) for entry in parsed.constraints],
        )

    def selected(self, section: Section) -> tuple[_ParsedSection, list[_BatchEntry]]:
        if section == "objects":
            if self.objects_section is None:
                raise SourceError(
                    "source.edit.object-schema",
                    "legacy Sketch source must be migrated before adding objects",
                )
            return self.objects_section, self.objects
        if section == "entities":
            return self.entities_section, self.entities
        return self.constraints_section, self.constraints

    def relocate(self, edits: Sequence[_Splice]) -> None:
        def shift(position: int) -> int:
            return position + sum(
                len(replacement) - (end - start)
                for start, end, replacement in edits
                if end <= position
            )

        for entries in (self.objects, self.entities, self.constraints):
            for entry in entries:
                offset = shift(entry.span.start) - entry.span.start
                entry.span = _Span(entry.span.start + offset, entry.span.end + offset)

        def section(value: _ParsedSection) -> _ParsedSection:
            start = shift(value.span.start)
            close = shift(value.close)
            return _ParsedSection(value.kind, _Span(start, close + 1), close)

        if self.objects_section is not None:
            self.objects_section = section(self.objects_section)
        self.entities_section = section(self.entities_section)
        self.constraints_section = section(self.constraints_section)


def _target(entries: Sequence[_BatchEntry], stable: str) -> int:
    found = next(
        (index for index, entry in enumerate(entries) if entry.id == stable), None
    )
    if found is None:
        raise SourceError("source.edit.missing-target", "edit target is missing")
    return found


def _apply_operation(
    state: _BatchState,
    bindings: Mapping[str, str],
    edit: SourceEdit,
) -> None:
    if edit.section not in {"objects", "entities", "constraints"}:
        raise SourceError("source.edit.invalid-section", "edit section is invalid")
    section, entries = state.selected(edit.section)
    spans = tuple(entry.span for entry in entries)

    replacement: bytes | None = None
    replacement_id: str | None = None
    if isinstance(edit, (AppendCall, ReplaceCall)):
        replacement, replacement_id = _replacement(edit.call, edit.section, bindings)

    if isinstance(edit, AppendCall):
        if replacement is None or replacement_id is None:
            raise SourceError("source.edit.internal", "append call is missing")
        if any(entry.id == replacement_id for entry in entries):
            raise SourceError(
                "source.edit.duplicate-stable-id", "appended stable ID already exists"
            )
        splices, call_span = _append_edits(state.source, section, spans, replacement)
        state.source = _apply_bytes(state.source, splices)
        state.relocate(splices)
        _, relocated = state.selected(edit.section)
        relocated.append(_BatchEntry(replacement_id, call_span))
        return

    if isinstance(edit, ReplaceCall):
        if replacement is None or replacement_id is None:
            raise SourceError("source.edit.internal", "replacement target is missing")
        if replacement_id != edit.id:
            raise SourceError(
                "source.edit.identity-change", "replacement cannot change the stable ID"
            )
        index = _target(entries, edit.id)
        start = entries[index].span.start
        target_indent = _line_prefix(state.source, start)
        formatted = replacement
        if target_indent is not None:
            line_end = _line_end(state.source, entries[index].span.end)
            newline = (
                b"\r\n"
                if line_end >= 2 and state.source[line_end - 2 : line_end] == b"\r\n"
                else b"\n"
            )
            formatted = _indented_continuations(replacement, target_indent, newline)
        splices = [(start, entries[index].span.end, formatted)]
        state.source = _apply_bytes(state.source, splices)
        state.relocate(splices)
        entries[index].span = _Span(start, start + len(formatted))
        return

    index = _target(entries, edit.id)
    splices = _delete_edits(state.source, section, spans, index)
    state.source = _apply_bytes(state.source, splices)
    state.relocate(splices)
    entries.pop(index)


def _validate_batch_size(size: int) -> None:
    if size > MAXIMUM_SOURCE_EDIT_BATCH:
        raise SourceError(
            "source.edit.batch-limit",
            "edit batch exceeds the synchronous limit; replace the whole source",
        )


def _apply_parsed_edits(
    parsed: _ParsedRecognition, function: str, edits: Sequence[SourceEdit]
) -> _ParsedRecognition:
    if not edits:
        raise SourceError("source.edit.empty-batch", "edit batch is empty")
    _validate_batch_size(len(edits))
    state = _BatchState.create(parsed)
    for edit in edits:
        _apply_operation(state, parsed.bindings, edit)

    updated_source = state.source.decode()
    # The original function and every isolated replacement have passed unit-
    # binding validation. Splices cannot change any other token.
    updated = _parse(
        updated_source,
        function,
        units_already_validated=True,
        prepared_source=_Source.from_data(state.source),
    )
    if updated is None or updated.public is None:
        raise SourceError(
            "source.edit.lost-recognition", "edit made the sketch unrecognizable"
        )
    return updated


def _session(parsed: _ParsedRecognition) -> SourceEditSession:
    if parsed.public is None:
        raise SourceError("source.edit.internal", "session recognition is missing")
    return SourceEditSession(
        parsed.public.function,
        parsed.public.source_digest,
        parsed.public,
        parsed,
    )


def open_edit_session(source: str, function: str) -> SourceEditSession | None:
    """Recognize and explicitly own one editable source revision."""
    parsed = _parse(source, function)
    return None if parsed is None else _session(parsed)


def apply_edit(
    source: str, function: str, expected_digest: str, edit: SourceEdit
) -> SourceEditResult:
    """Apply one structural edit if source still matches its observed digest."""
    parsed_source = _Source.create(source)
    prior_digest = _source_digest_bytes(parsed_source.data)
    if prior_digest != expected_digest:
        raise SourceError("source.edit.stale", "source changed after it was observed")
    if edit.section not in {"objects", "entities", "constraints"}:
        raise SourceError("source.edit.invalid-section", "edit section is invalid")
    parsed = _parse(
        source, function, materialize_public=False, prepared_source=parsed_source
    )
    if parsed is None:
        raise SourceError(
            "source.edit.unrecognized", "function is not a recognized sketch"
        )
    updated = _apply_parsed_edits(parsed, function, (edit,))
    updated_source = updated.source.data.decode()
    if updated.public is None:
        raise SourceError("source.edit.internal", "updated recognition is missing")
    return SourceEditResult(
        updated_source,
        prior_digest,
        updated.public.source_digest,
        updated.public,
    )


__all__ = [
    "MAXIMUM_SOURCE_EDIT_BATCH",
    "AppendCall",
    "DeleteCall",
    "Recognition",
    "RecognizedCall",
    "ReplaceCall",
    "Section",
    "SourceEditResult",
    "SourceEditSession",
    "SourceError",
    "SourceSpan",
    "apply_edit",
    "open_edit_session",
    "recognize",
    "source_digest",
]
