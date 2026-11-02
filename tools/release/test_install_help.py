"""Player help must describe the portable default and non-destructive migration."""
import unittest
import make_release

class InstallationHelpTests(unittest.TestCase):

    def test_both_packages_describe_portable_destination_and_remembered_choice(self):
        for platform, text in (('Windows', make_release.README_WINDOWS), ('Linux', make_release.README_LINUX)):
            with self.subTest(platform=platform):
                current, separator, migration = text.partition('Migration')
                self.assertTrue(separator, 'old installations need migration guidance')
                self.assertIn('beside', current.lower())
                self.assertIn('Choose folder', current)
                self.assertIn('dw5e-install.json', current)
                self.assertNotIn('LOCALAPPDATA', current)
                self.assertNotIn('XDG_DATA_HOME', current)
                self.assertIn('backup', migration.lower())
                self.assertIn('copy', migration.lower())
                self.assertIn('--data-dir', text)
                self.assertIn('DW5E_DATA_DIR', text)
if __name__ == '__main__':
    unittest.main()
