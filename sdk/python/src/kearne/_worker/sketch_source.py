"""Bounded structural source transforms for recognized Sketch functions."""

from __future__ import annotations

import keyword
from importlib import import_module
from types import ModuleType
from typing import cast

from kearne._sketch_schema import HELPERS
from kearne._wire import (
    WireMessage as _WireMessage,
)
from kearne._wire import (
    child as _child,
)
from kearne._wire import (
    reject_unknown,
    uuid7_text,
)
from kearne._wire import (
    repeated as _repeated,
)
from kearne._wire import (
    scalar as _scalar,
)
from kearne._wire import (
    set_scalar as _set_scalar,
)
from kearne.sketch_source import emit_call, values_from_source
from kearne.sketch_values import Constraint, Entity
from kearne.sketch_wire import (
    SketchWireError,
    definition_values_from_wire,
    definition_values_to_wire,
)
from kearne.source import (
    MAXIMUM_SOURCE_EDIT_BATCH,
    AppendCall,
    DeleteCall,
    ReplaceCall,
    Section,
    SourceEdit,
    SourceError,
    open_edit_session,
    source_digest,
)

MAXIMUM_SOURCE_BYTES = 65_536
MAXIMUM_TRANSFORM_BYTES = 131_328


def _wire_module() -> ModuleType:
    try:
        return import_module("kearne.api.v1.worker_pb2")
    except ModuleNotFoundError as error:
        raise SourceError(
            "worker.bindings-missing",
            "generated Kearne worker bindings are unavailable",
        ) from error


def _message_type(name: str) -> type[_WireMessage]:
    return cast(type[_WireMessage], getattr(_wire_module(), name))


def _reject_unknown(message: _WireMessage) -> None:
    if reject_unknown(message):
        raise SourceError(
            "worker.unknown-field", "worker job contains an unsupported field"
        )


def _enum_name(message: _WireMessage, field: str) -> str:
    number = int(cast(int, _scalar(message, field)))
    value = message.DESCRIPTOR.fields_by_name[field].enum_type.values_by_number.get(
        number
    )
    if value is None or value.name.endswith("_UNSPECIFIED"):
        raise SourceError("worker.invalid-enum", "worker job enum is unsupported")
    return value.name


def _uuid(message: _WireMessage) -> str:
    try:
        return uuid7_text(message)
    except ValueError as error:
        raise SourceError("worker.invalid-id", "worker job ID is invalid") from error


def _digest(message: _WireMessage) -> str:
    algorithm = cast(str, _scalar(message, "algorithm"))
    value = cast(bytes, _scalar(message, "value"))
    result = f"{algorithm}:{value.hex()}"
    if (
        not 1 <= len(algorithm) <= 32
        or not all(
            "a" <= character <= "z" or "0" <= character <= "9" or character == "-"
            for character in algorithm
        )
        or len(value) != 32
    ):
        raise SourceError("worker.invalid-digest", "worker job digest is invalid")
    return result


def _write_digest(message: _WireMessage, digest: str) -> None:
    algorithm, separator, value = digest.partition(":")
    if not separator:
        raise SourceError("worker.invalid-digest", "worker result digest is invalid")
    _set_scalar(message, "algorithm", algorithm)
    _set_scalar(message, "value", bytes.fromhex(value))


def _function_name(value: object) -> str:
    name = cast(str, value)
    if (
        not name
        or len(name.encode()) > 255
        or not name.isidentifier()
        or keyword.iskeyword(name)
    ):
        raise SourceError(
            "source.sketch.invalid-function", "Sketch function name is invalid"
        )
    return name


def create_sketch_source(function_name: str) -> str:
    """Create the minimal recognized native source for an empty Sketch."""
    name = _function_name(function_name)
    helpers = ", ".join(sorted(HELPERS))
    result = f"""from build123d import Sketch
from kearne.sketch import SketchDefinition, SketchPlane, {helpers}
from kearne.units import m, rad

def {name}(plane: SketchPlane) -> Sketch:
    return SketchDefinition(
        plane=plane,
        entities=(),
        constraints=(),
    ).build()
"""
    if len(result.encode()) > MAXIMUM_SOURCE_BYTES:
        raise SourceError("source.sketch.byte-limit", "Sketch source is too large")
    if open_edit_session(result, name) is None:
        raise SourceError(
            "source.sketch.internal", "generated Sketch source is not recognized"
        )
    return result


def _target_values(
    target: _WireMessage,
) -> tuple[
    dict[str, tuple[str, Entity | Constraint]],
    str,
    tuple[Entity, ...],
    tuple[Constraint, ...],
]:
    decoded = definition_values_from_wire(target)
    values: dict[str, tuple[str, Entity | Constraint]] = {
        value.id: ("entities", value) for value in decoded.entities
    }
    values.update(
        (value.id, ("constraints", value)) for value in decoded.constraints
    )
    return values, decoded.source_digest, decoded.entities, decoded.constraints


def _source_edit(
    message: _WireMessage,
    current: dict[str, str],
    target: dict[str, tuple[str, Entity | Constraint]],
) -> tuple[SourceEdit, str | None]:
    if not all(message.HasField(field) for field in ("action", "section", "target_id")):
        raise SourceError("worker.required-field", "source edit is incomplete")
    action = (
        _enum_name(message, "action").removeprefix("SKETCH_SOURCE_EDIT_ACTION_").lower()
    )
    section = (
        _enum_name(message, "section").removeprefix("SKETCH_SOURCE_SECTION_").lower()
    )
    stable = _uuid(_child(message, "target_id"))
    prior_section = current.get(stable)
    desired = target.get(stable)
    if action == "append":
        if prior_section is not None or desired is None or desired[0] != section:
            raise SourceError("source.edit.invalid-append", "append target is invalid")
        code = emit_call(desired[1])
        return AppendCall(cast(Section, section), code), code
    if action == "replace":
        if prior_section != section or desired is None or desired[0] != section:
            raise SourceError(
                "source.edit.invalid-replace", "replacement target is invalid"
            )
        code = emit_call(desired[1])
        return ReplaceCall(cast(Section, section), stable, code), code
    if action == "delete":
        if prior_section != section or desired is not None:
            raise SourceError("source.edit.invalid-delete", "delete target is invalid")
        return DeleteCall(cast(Section, section), stable), None
    raise SourceError("worker.invalid-enum", "source edit action is unsupported")


def _edit(
    message: _WireMessage,
) -> tuple[str, str, tuple[Entity, ...], tuple[Constraint, ...]]:
    required = ("source_utf8", "function_name", "expected_prior", "target")
    if not all(message.HasField(field) for field in required):
        raise SourceError("worker.required-field", "source edit batch is incomplete")
    raw = cast(bytes, _scalar(message, "source_utf8"))
    if not raw or len(raw) > MAXIMUM_SOURCE_BYTES:
        raise SourceError("source.sketch.byte-limit", "Sketch source is too large")
    try:
        source = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise SourceError("source.python.encoding", "source is not UTF-8") from error
    function = _function_name(_scalar(message, "function_name"))
    expected = _digest(_child(message, "expected_prior"))
    if source_digest(source) != expected:
        raise SourceError("source.edit.stale", "source changed after it was observed")
    target, target_digest, entities, constraints = _target_values(
        _child(message, "target")
    )
    if target_digest != expected:
        raise SourceError(
            "source.edit.target-stale", "target was derived from another source"
        )
    session = open_edit_session(source, function)
    if session is None:
        raise SourceError(
            "source.edit.unrecognized", "function is not a recognized Sketch"
        )
    current = {value.id: "entities" for value in session.recognition.entities} | {
        value.id: "constraints" for value in session.recognition.constraints
    }
    messages = tuple(_repeated(message, "edits"))
    if not 1 <= len(messages) <= MAXIMUM_SOURCE_EDIT_BATCH:
        raise SourceError(
            "source.edit.batch-size", "source edit batch has an invalid size"
        )
    operations: list[SourceEdit] = []
    edited_code: dict[str, str | None] = {}
    for value in messages:
        stable = _uuid(_child(value, "target_id"))
        if stable in edited_code:
            raise SourceError(
                "source.edit.duplicate-target", "source edit target is duplicated"
            )
        operation, code = _source_edit(value, current, target)
        operations.append(operation)
        edited_code[stable] = code
    updated = session.apply(expected, operations)
    final = {
        value.id: ("entities", value.code) for value in updated.recognition.entities
    } | {
        value.id: ("constraints", value.code)
        for value in updated.recognition.constraints
    }
    if set(final) != set(target):
        raise SourceError(
            "source.edit.target-mismatch",
            "edited source does not match target identity",
        )
    for stable, code in edited_code.items():
        if code is None:
            if stable in final:
                raise SourceError(
                    "source.edit.target-mismatch", "deleted source identity remains"
                )
        elif final.get(stable) != (target[stable][0], code):
            raise SourceError(
                "source.edit.target-mismatch", "edited source call changed"
            )
    if len(updated.source.encode()) > MAXIMUM_SOURCE_BYTES:
        raise SourceError("source.sketch.byte-limit", "Sketch source is too large")
    return updated.source, updated.source_digest, entities, constraints


def _replace(
    message: _WireMessage,
) -> tuple[str, str, tuple[Entity, ...], tuple[Constraint, ...]]:
    required = ("source_utf8", "function_name", "expected_prior")
    if not all(message.HasField(field) for field in required):
        raise SourceError("worker.required-field", "source replacement is incomplete")
    raw = cast(bytes, _scalar(message, "source_utf8"))
    if not raw or len(raw) > MAXIMUM_SOURCE_BYTES:
        raise SourceError("source.sketch.byte-limit", "Sketch source is too large")
    try:
        source = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise SourceError("source.python.encoding", "source is not UTF-8") from error
    function = _function_name(_scalar(message, "function_name"))
    _digest(_child(message, "expected_prior"))
    digest, entities, constraints = values_from_source(source, function)
    return source, digest, entities, constraints


def _failure(code: str) -> _WireMessage:
    result = _message_type("SketchSourceTransformResult")()
    diagnostic = _repeated(_child(result, "failure"), "diagnostics").add()
    _set_scalar(diagnostic, "code", code)
    severity = diagnostic.DESCRIPTOR.fields_by_name["severity"].enum_type
    error = next(
        value
        for value in severity.values_by_number.values()
        if value.name == "SEVERITY_ERROR"
    )
    _set_scalar(diagnostic, "severity", error.number)
    return result


def process_transform(job: _WireMessage) -> _WireMessage:
    """Execute one validated transform without retaining project state."""
    try:
        if job.DESCRIPTOR.full_name != "kearne.api.v1.SketchSourceTransformJob":
            raise SourceError("worker.wrong-job", "worker job type is unsupported")
        if job.ByteSize() > MAXIMUM_TRANSFORM_BYTES:
            raise SourceError("worker.byte-limit", "worker job is too large")
        _reject_unknown(job)
        operation = job.WhichOneof("operation")
        if operation == "create":
            create = _child(job, "create")
            if not create.HasField("function_name"):
                raise SourceError("worker.required-field", "create job is incomplete")
            source = create_sketch_source(cast(str, _scalar(create, "function_name")))
            digest = source_digest(source)
            entities: tuple[Entity, ...] = ()
            constraints: tuple[Constraint, ...] = ()
        elif operation == "edit":
            source, digest, entities, constraints = _edit(_child(job, "edit"))
        elif operation == "replace":
            source, digest, entities, constraints = _replace(
                _child(job, "replace")
            )
        else:
            raise SourceError("worker.required-oneof", "worker operation is missing")
        result = _message_type("SketchSourceTransformResult")()
        success = _child(result, "success")
        _set_scalar(success, "source_utf8", source.encode())
        _write_digest(_child(success, "source_digest"), digest)
        _child(success, "definition").CopyFrom(
            definition_values_to_wire(entities, constraints, digest)
        )
        return result
    except (SketchWireError, SourceError) as error:
        return _failure(error.code)


__all__ = [
    "MAXIMUM_SOURCE_BYTES",
    "MAXIMUM_TRANSFORM_BYTES",
    "create_sketch_source",
    "process_transform",
]
