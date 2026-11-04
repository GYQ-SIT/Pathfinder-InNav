#!/usr/bin/env python3
"""Checks JSON files against a JSON Schema (guideline C5). Standard library only, so it runs on any runner and needs nothing installed.

It understands the part of JSON Schema (draft-07) that the schemas of this repository use, and refuses a schema that uses anything else, so
that a keyword is never silently ignored:

  type (one name or a list), enum, const, required, properties, additionalProperties (true, false or a schema), items (one schema),
  minItems, maxItems, minimum, maximum, exclusiveMinimum, minLength, pattern, $ref (to "#/definitions/name" or "#/$defs/name"), and the
  annotations $schema, $id, title, description, default, examples, $comment, definitions, $defs.

Usage:
  python scripts/validate_schema.py SCHEMA FILE [FILE ...]     validate the files (a file may be a glob); exit status 1 on any error
  python scripts/validate_schema.py --self-test                 check the validator itself
"""
import glob
import json
import re
import sys

ANNOTATIONS = {"$schema", "$id", "title", "description", "default", "examples", "$comment", "definitions", "$defs"}
KEYWORDS = {"type", "enum", "const", "required", "properties", "additionalProperties", "items", "minItems", "maxItems", "minimum", "maximum",
            "exclusiveMinimum", "minLength", "pattern", "$ref"}


def load(path):
    with open(path, "rb") as handle:
        # Older editor saves can be UTF-16 with a byte order mark.
        return json.loads(handle.read())


def type_matches(value, name):
    if name == "object":
        return isinstance(value, dict)
    if name == "array":
        return isinstance(value, list)
    if name == "string":
        return isinstance(value, str)
    if name == "boolean":
        return isinstance(value, bool)
    if name == "null":
        return value is None
    if name == "integer":
        return (isinstance(value, int) and not isinstance(value, bool)) or (isinstance(value, float) and value.is_integer())
    if name == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    raise ValueError(f"unknown type name {name!r} in the schema")


def resolve(root, reference):
    if not reference.startswith("#/"):
        raise ValueError(f"only local references are understood, not {reference!r}")
    node = root
    for part in reference[2:].split("/"):
        node = node[part]
    return node


def check_schema(schema, where="#"):
    """Refuses a schema that uses a keyword this validator does not know."""
    if isinstance(schema, bool):
        return
    for key, value in schema.items():
        if key not in KEYWORDS and key not in ANNOTATIONS:
            raise ValueError(f"{where}: the keyword {key!r} is not supported by scripts/validate_schema.py")
        if key == "properties":
            for name, sub in value.items():
                check_schema(sub, f"{where}/properties/{name}")
        elif key in ("definitions", "$defs"):
            for name, sub in value.items():
                check_schema(sub, f"{where}/{key}/{name}")
        elif key == "items" or (key == "additionalProperties" and isinstance(value, dict)):
            check_schema(value, f"{where}/{key}")


def validate(value, schema, root, path="$"):
    """Returns a list of "path: what is wrong"; empty when the value is valid."""
    if schema is True:
        return []
    if schema is False:
        return [f"{path}: not allowed"]
    if "$ref" in schema:
        return validate(value, resolve(root, schema["$ref"]), root, path)
    errors = []
    if "type" in schema:
        names = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        if not any(type_matches(value, name) for name in names):
            return [f"{path}: expected {' or '.join(names)}, found {type(value).__name__}"]
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: {value!r} is not one of {schema['enum']}")
    if "const" in schema and value != schema["const"]:
        errors.append(f"{path}: must be {schema['const']!r}")
    if isinstance(value, bool):
        return errors
    if isinstance(value, (int, float)):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{path}: {value} is below the minimum {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            errors.append(f"{path}: {value} is above the maximum {schema['maximum']}")
        if "exclusiveMinimum" in schema and value <= schema["exclusiveMinimum"]:
            errors.append(f"{path}: {value} must be above {schema['exclusiveMinimum']}")
    if isinstance(value, str):
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append(f"{path}: is shorter than {schema['minLength']} characters")
        if "pattern" in schema and not re.search(schema["pattern"], value):
            errors.append(f"{path}: {value!r} does not match {schema['pattern']}")
    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errors.append(f"{path}: has {len(value)} items, at least {schema['minItems']} needed")
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            errors.append(f"{path}: has {len(value)} items, at most {schema['maxItems']} allowed")
        if "items" in schema:
            for index, item in enumerate(value):
                errors += validate(item, schema["items"], root, f"{path}[{index}]")
    if isinstance(value, dict):
        for name in schema.get("required", []):
            if name not in value:
                errors.append(f"{path}: the field {name!r} is missing")
        properties = schema.get("properties", {})
        for name, item in value.items():
            if name in properties:
                errors += validate(item, properties[name], root, f"{path}.{name}")
            elif "additionalProperties" in schema:
                extra = schema["additionalProperties"]
                if extra is False:
                    errors.append(f"{path}: the field {name!r} is not known")
                elif extra is not True:
                    errors += validate(item, extra, root, f"{path}.{name}")
    return errors


def validate_file(schema_path, data_path):
    schema = load(schema_path)
    check_schema(schema)
    return validate(load(data_path), schema, schema)


def self_test():
    schema = {"type": "object", "required": ["a"], "additionalProperties": False,
              "properties": {"a": {"type": "integer", "minimum": 1}, "b": {"type": "array", "minItems": 2, "items": {"$ref": "#/definitions/pt"}},
                             "c": {"enum": ["x", "y"]}}, "definitions": {"pt": {"type": "number"}}}
    cases = [
        ({"a": 1}, 0), ({"a": 1, "b": [1, 2.5], "c": "x"}, 0), ({}, 1), ({"a": 0}, 1), ({"a": 1.5}, 1), ({"a": True}, 1),
        ({"a": 1, "z": 1}, 1), ({"a": 1, "b": [1]}, 1), ({"a": 1, "b": [1, "s"]}, 1), ({"a": 1, "c": "q"}, 1), ([], 1),
    ]
    failed = 0
    for value, expected in cases:
        found = len(validate(value, schema, schema))
        if found != expected:
            failed += 1
            print(f"self-test: {value!r} gave {found} errors, expected {expected}")
    try:
        check_schema({"oneOf": []})
        failed += 1
        print("self-test: an unsupported keyword was accepted")
    except ValueError:
        pass
    print("self-test: " + ("failed" if failed else f"all {len(cases) + 1} cases pass"))
    return 1 if failed else 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if len(argv) < 2:
        print(__doc__)
        return 2
    schema_path, patterns = argv[0], argv[1:]
    files = sorted({match for pattern in patterns for match in (glob.glob(pattern) or [pattern])})
    problems = 0
    for path in files:
        try:
            errors = validate_file(schema_path, path)
        except (OSError, ValueError, KeyError) as error:
            errors = [f"cannot be checked: {error}"]
        for error in errors[:20]:
            print(f"{path}: {error}")
        if len(errors) > 20:
            print(f"{path}: ... and {len(errors) - 20} more")
        problems += 1 if errors else 0
        if not errors:
            print(f"{path}: valid")
    print(f"{len(files)} file(s) checked against {schema_path}, {problems} invalid.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
