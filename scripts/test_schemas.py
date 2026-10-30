#!/usr/bin/env python3
"""Tests of the JSON Schemas of docs/schemas/ (guideline C5: invalid input is refused with a clear error). Standard library only.

  python scripts/test_schemas.py

The files of the repository must be valid, and each kind of mistake made to a copy of one must be reported, with where it is.
"""
import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_schema as vs  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PROJECT_SCHEMA = os.path.join(ROOT, "docs", "schemas", "project.schema.json")
PROTOTYPE_SCHEMA = os.path.join(ROOT, "docs", "schemas", "mobile-prototype.schema.json")
PROJECT_FILE = os.path.join(ROOT, "docs", "test-data", "entrance-test-2-with-doors.json")
PROTOTYPE_FILE = os.path.join(ROOT, "InNavi", "Data", "Prototype", "mobile-prototype.json")


def errors_of(schema_path, data):
    schema = vs.load(schema_path)
    vs.check_schema(schema)
    return vs.validate(data, schema, schema)


class ProjectSchema(unittest.TestCase):
    def setUp(self):
        self.project = vs.load(PROJECT_FILE)

    def expect(self, mutate, *words):
        data = copy.deepcopy(self.project)
        mutate(data)
        errors = errors_of(PROJECT_SCHEMA, data)
        self.assertTrue(errors, "the mistake was not reported")
        text = "\n".join(errors)
        for word in words:
            self.assertIn(word, text)

    def test_the_test_project_is_valid(self):
        self.assertEqual(errors_of(PROJECT_SCHEMA, self.project), [])

    def test_an_empty_project_is_valid(self):
        self.assertEqual(errors_of(PROJECT_SCHEMA, {"buildings": []}), [])

    def test_a_file_without_buildings(self):
        self.expect(lambda d: d.pop("buildings"), "'buildings' is missing")

    def test_a_version_that_is_not_a_number(self):
        self.expect(lambda d: d.update(version="one"), "$.version", "expected integer")

    def test_a_field_that_is_not_known(self):
        self.expect(lambda d: d.update(projectNmae="x"), "'projectNmae' is not known")

    def test_a_building_without_a_footprint(self):
        self.expect(lambda d: d["buildings"][0].pop("footprint"), "$.buildings[0]", "'footprint' is missing")

    def test_a_footprint_with_two_points(self):
        self.expect(lambda d: d["buildings"][0].update(footprint=[[0, 0], [1, 1]]), "$.buildings[0].footprint", "at least 3")

    def test_a_point_with_three_numbers(self):
        self.expect(lambda d: d["buildings"][0]["footprint"].__setitem__(0, [0, 0, 0]), "$.buildings[0].footprint[0]", "at most 2")

    def test_a_floor_below_the_ground(self):
        self.expect(lambda d: d["buildings"][0]["interior"]["floors"][0].update(level=0), ".level", "below the minimum")

    def test_an_opening_of_a_kind_that_does_not_exist(self):
        self.expect(lambda d: d["buildings"][0]["interior"]["floors"][0]["rooms"][0]["openings"][0].update(type="gate"), "'gate' is not one of")

    def test_an_opening_with_a_misspelt_field(self):
        def mutate(d):
            opening = d["buildings"][0]["interior"]["floors"][0]["rooms"][0]["openings"][0]
            opening["edgeindex"] = opening.pop("edgeIndex")
        self.expect(mutate, "'edgeindex' is not known", "'edgeIndex' is missing")

    def test_a_connector_of_a_kind_that_does_not_exist(self):
        self.expect(lambda d: d["buildings"][0]["interior"]["floors"][0]["navNodes"][0].update(type="lift"), "'lift' is not one of")

    def test_a_room_that_is_neither_public_nor_private(self):
        self.expect(lambda d: d["buildings"][0]["interior"]["floors"][0]["rooms"][0].update(access="staff"), "'staff' is not one of")

    def test_an_outer_door_of_a_kind_that_does_not_exist(self):
        self.expect(lambda d: d["buildings"][0]["interior"]["entrances"][0].update(kind="side"), "'side' is not one of")

    def test_a_width_of_zero(self):
        self.expect(lambda d: d["buildings"][0]["interior"]["entrances"][0].update(width=0), "must be above 0")

    def test_the_mistake_says_where_it_is(self):
        data = copy.deepcopy(self.project)
        data["buildings"][1]["interior"]["floors"][0]["rooms"][3]["access"] = "staff"
        self.assertTrue(any(error.startswith("$.buildings[1].interior.floors[0].rooms[3].access:") for error in errors_of(PROJECT_SCHEMA, data)))


class PrototypeSchema(unittest.TestCase):
    def setUp(self):
        self.prototype = vs.load(PROTOTYPE_FILE)

    def expect(self, mutate, *words):
        data = copy.deepcopy(self.prototype)
        mutate(data)
        errors = errors_of(PROTOTYPE_SCHEMA, data)
        self.assertTrue(errors, "the mistake was not reported")
        for word in words:
            self.assertIn(word, "\n".join(errors))

    def test_the_prototype_is_valid(self):
        self.assertEqual(errors_of(PROTOTYPE_SCHEMA, self.prototype), [])

    def test_data_that_does_not_say_it_is_made_up(self):
        self.expect(lambda d: d.update(synthetic=False), "$.synthetic", "must be True")

    def test_data_of_another_format(self):
        self.expect(lambda d: d.update(format="project"), "$.format")

    def test_data_without_a_plan(self):
        self.expect(lambda d: d.pop("plan"), "'plan' is missing")

    def test_a_site_that_cannot_print_a_code(self):
        self.expect(lambda d: d["sites"][0].update(codePattern="E2"), "codePattern", "does not match")

    def test_a_level_without_a_label(self):
        self.expect(lambda d: d["sites"][0]["buildings"][0]["levels"][0].pop("label"), "'label' is missing")


if __name__ == "__main__":
    unittest.main(verbosity=1)
