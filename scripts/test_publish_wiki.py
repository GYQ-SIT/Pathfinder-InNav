#!/usr/bin/env python3
"""Tests of scripts/publish-wiki.py, the step that puts the built site in the web root. Standard library only.

  python scripts/test_publish_wiki.py

The swap needs the runner to write next to the web root; when it cannot (or the exchange is not supported) the contents are replaced in place.
"""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "publish-wiki.py")
spec = importlib.util.spec_from_file_location("publish_wiki", SCRIPT)
publish_wiki = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish_wiki)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class PublishWikiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.site = root / "site"
        write(self.site / "index.html", "new index")
        write(self.site / "guide" / "page.html", "new page")
        self.live = root / "www" / "wiki"
        write(self.live / "index.html", "old index")
        write(self.live / "old-only.html", "gone")
        write(self.live / "old-dir" / "x.html", "gone")

    def assert_site_is_live(self):
        self.assertEqual((self.live / "index.html").read_text(encoding="utf-8"), "new index")
        self.assertEqual((self.live / "guide" / "page.html").read_text(encoding="utf-8"), "new page")
        self.assertFalse((self.live / "old-only.html").exists())
        self.assertFalse((self.live / "old-dir").exists())

    def test_replace_contents_leaves_exactly_the_site(self):
        publish_wiki.replace_contents(self.site, self.live)
        self.assert_site_is_live()

    def test_a_parent_that_cannot_be_written_falls_back_to_replacing_the_contents(self):
        with mock.patch.object(publish_wiki.tempfile, "TemporaryDirectory", side_effect=PermissionError(13, "Permission denied")):
            publish_wiki.publish(self.site, self.live)
        self.assert_site_is_live()

    def test_an_exchange_that_is_not_supported_falls_back_to_replacing_the_contents(self):
        with mock.patch.object(publish_wiki, "exchange", side_effect=OSError(22, "Invalid argument")):
            publish_wiki.publish(self.site, self.live)
        self.assert_site_is_live()
        # Nothing of the staging is left next to the web root.
        self.assertEqual(sorted(p.name for p in self.live.parent.iterdir()), ["wiki"])

    def test_a_site_without_an_index_is_refused_and_the_web_root_is_not_touched(self):
        (self.site / "index.html").unlink()
        with self.assertRaises(ValueError):
            publish_wiki.publish(self.site, self.live)
        self.assertEqual((self.live / "index.html").read_text(encoding="utf-8"), "old index")


if __name__ == "__main__":
    unittest.main()
