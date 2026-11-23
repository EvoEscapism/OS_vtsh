#!/usr/bin/env python3
"""Integration tests for VTSH. The tested shell is launched without shell=True."""

import os
import re
import select
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VTSH = Path(os.environ.get("VTSH_BIN", ROOT / "build" / "bin" / "vtsh"))
NOFILE_PROBE = Path(
    os.environ.get("NOFILE_PROBE_BIN", ROOT / "build" / "bin" / "nofile_probe")
)
SIGNAL_PROBE = Path(
    os.environ.get("SIGNAL_PROBE_BIN", ROOT / "build" / "bin" / "signal_probe")
)


def run_shell(commands: str, *, extra_env=None, timeout: float = 8.0):
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    return subprocess.run(
        [str(VTSH)],
        input=commands,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        timeout=timeout,
        check=False,
    )


def read_shell_nofile_limits(pid: int):
    """Return (soft, hard) from /proc/PID/limits for RLIMIT_NOFILE."""
    limits_path = Path(f"/proc/{pid}/limits")
    for line in limits_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("Max open files"):
            fields = line.split()
            return fields[3], fields[4]
    raise AssertionError(f"Max open files row not found in {limits_path}")


class VtshTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not VTSH.is_file():
            raise RuntimeError(f"vtsh executable not found: {VTSH}. Build the project first.")
        cls.temp_dir = tempfile.TemporaryDirectory(prefix="vtsh-tests-")
        cls.temp_path = Path(cls.temp_dir.name)
        cls.no_shebang = cls.temp_path / "executable_without_shebang"
        cls.no_shebang.write_text("This is deliberately not a script.\n", encoding="utf-8")
        cls.no_shebang.chmod(0o755)
        cls.not_executable = cls.temp_path / "not_executable"
        cls.not_executable.write_text("not executable\n", encoding="utf-8")
        cls.not_executable.chmod(0o644)

    @classmethod
    def tearDownClass(cls):
        cls.temp_dir.cleanup()

    def test_empty_line_and_eof(self):
        blank_line = run_shell("\n")
        empty_eof = run_shell("")
        self.assertEqual(blank_line.returncode, 0, msg=blank_line.stderr)
        self.assertEqual(empty_eof.returncode, 0, msg=empty_eof.stderr)
        self.assertEqual(blank_line.stdout, "")
        self.assertEqual(empty_eof.stdout, "")

    def test_absolute_path_and_arguments(self):
        result = run_shell("/bin/echo hello from vtsh\n")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), "hello from vtsh")

    def test_path_search_and_assignment_examples(self):
        result = run_shell(
            "printf one ; printf two\n"
            "false && echo not-run\n"
            "true && echo recovered\n",
            extra_env={"PATH": "/usr/bin:/bin"},
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("onetwo", result.stdout)
        self.assertIn("recovered", result.stdout)
        self.assertNotIn("not-run", result.stdout)

    def test_extra_spaces_and_no_arguments(self):
        result = run_shell("   /bin/true     \n    /bin/echo     spaced    words   \n")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), "spaced words")

    def test_and_runs_after_success(self):
        result = run_shell("true && echo executed\n", extra_env={"PATH": "/usr/bin:/bin"})
        self.assertIn("executed", result.stdout)
        self.assertEqual(result.returncode, 0)

    def test_and_skips_after_failure_and_preserves_status(self):
        result = run_shell(
            "false && echo must_not_appear\n",
            extra_env={"PATH": "/usr/bin:/bin"},
        )
        self.assertNotIn("must_not_appear", result.stdout)
        self.assertEqual(result.returncode, 1)

    def test_or_runs_after_failure(self):
        result = run_shell("false || echo fallback\n", extra_env={"PATH": "/usr/bin:/bin"})
        self.assertEqual(result.stdout.strip(), "fallback")
        self.assertEqual(result.returncode, 0)

    def test_or_skips_after_success_and_preserves_status(self):
        result = run_shell(
            "true || echo must_not_appear\n",
            extra_env={"PATH": "/usr/bin:/bin"},
        )
        self.assertNotIn("must_not_appear", result.stdout)
        self.assertEqual(result.returncode, 0)

    def test_sequence_runs_after_failure(self):
        result = run_shell("false ; /bin/echo after-sequence\n")
        self.assertEqual(result.stdout.strip(), "after-sequence")
        self.assertEqual(result.returncode, 0)

    def test_sequence_runs_after_success(self):
        result = run_shell("true ; /bin/echo after-success\n")
        self.assertEqual(result.stdout.strip(), "after-success")
        self.assertEqual(result.returncode, 0)

    def test_trailing_semicolon_is_accepted(self):
        result = run_shell("/bin/echo trailing ;   \n")
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.strip(), "trailing")

    def test_environment_is_inherited(self):
        result = run_shell(
            "/usr/bin/printenv VTSH_TEST_VALUE\n",
            extra_env={"VTSH_TEST_VALUE": "environment-is-inherited"},
        )
        self.assertEqual(result.stdout.strip(), "environment-is-inherited")

    def test_missing_program_returns_127_and_shell_continues(self):
        result = run_shell(
            "/definitely/not/a/vtsh-command\n/bin/echo shell-alive\n"
        )
        self.assertIn("shell-alive", result.stdout)
        self.assertIn("не удалось выполнить", result.stderr)
        self.assertEqual(result.returncode, 0)

    def test_missing_program_exit_status_is_127(self):
        result = run_shell("vtsh_command_that_does_not_exist_908172\n")
        self.assertIn("не удалось выполнить", result.stderr)
        self.assertEqual(result.returncode, 127)

    def test_found_but_non_executable_program_returns_126(self):
        result = run_shell(f"{self.not_executable}\n")
        self.assertIn("не удалось выполнить", result.stderr)
        self.assertEqual(result.returncode, 126)

    def test_executable_without_shebang_returns_126(self):
        result = run_shell(f"{self.no_shebang}\n")
        self.assertIn("не удалось выполнить", result.stderr)
        self.assertEqual(result.returncode, 126)

    def test_exec_argument_list_too_long_returns_126(self):
        # Linux rejects a single argv string above MAX_ARG_STRLEN with E2BIG.
        oversized_argument = "x" * 200_000
        result = run_shell(f"/bin/true {oversized_argument}\n", timeout=12.0)
        self.assertIn("не удалось выполнить", result.stderr)
        self.assertEqual(result.returncode, 126)

    def test_signal_termination_status(self):
        if not SIGNAL_PROBE.is_file():
            self.skipTest(f"signal_probe executable not found: {SIGNAL_PROBE}")
        result = run_shell(f"{SIGNAL_PROBE}\n")
        self.assertEqual(result.returncode, 128 + signal.SIGTERM, msg=result.stderr)

    def test_cd_changes_directory_and_pwd_environment(self):
        result = run_shell("cd /tmp\n/bin/pwd\n/usr/bin/printenv PWD\n")
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["/tmp", "/tmp"])

    def test_invalid_syntax_does_not_kill_shell(self):
        result = run_shell("/bin/echo x &&\n/bin/echo still-alive\n")
        self.assertIn("после оператора ожидалась команда", result.stderr)
        self.assertIn("still-alive", result.stdout)
        self.assertEqual(result.returncode, 0)

    def test_pipe_and_background_are_reported_as_unsupported(self):
        result = run_shell(
            "/bin/echo x | /bin/wc\n/bin/sleep 1 &\n/bin/echo alive\n"
        )
        self.assertIn("конвейеры", result.stderr)
        self.assertIn("фоновый оператор", result.stderr)
        self.assertIn("alive", result.stdout)

    def test_unsupported_shell_syntax_is_treated_as_plain_arguments(self):
        redirected_file = self.temp_path / "must_not_be_created.txt"
        result = run_shell(
            f'/bin/echo "two words" ; '
            f'/bin/echo $VTSH_UNEXPANDED ; '
            f'/bin/echo *.vtsh-no-match ; '
            f'/bin/echo marker > {redirected_file}\n'
            f'/bin/echo one\\ two\n'
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn('"two words"', result.stdout)
        self.assertIn("$VTSH_UNEXPANDED", result.stdout)
        self.assertIn("*.vtsh-no-match", result.stdout)
        self.assertIn(f"marker > {redirected_file}", result.stdout)
        self.assertIn("one\\ two", result.stdout)
        self.assertFalse(redirected_file.exists(), "'>' must not perform redirection")

    def test_nofile_limit_is_applied_to_child_and_reaches_emfile(self):
        if not NOFILE_PROBE.is_file():
            self.skipTest(f"nofile_probe executable not found: {NOFILE_PROBE}")
        result = run_shell(f"limit nofile 32\n{NOFILE_PROBE}\n")
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("RLIMIT_NOFILE inside helper: soft=32 hard=32", result.stdout)
        self.assertIn("open() failed with EMFILE as expected", result.stdout)
        match = re.search(r"Additional descriptors opened before EMFILE: (\d+)", result.stdout)
        self.assertIsNotNone(match, msg=result.stdout)
        opened = int(match.group(1))
        self.assertGreater(opened, 0)
        self.assertLessEqual(opened, 29)

    def test_nofile_setting_does_not_change_shell_proc_limits(self):
        process = subprocess.Popen(
            [str(VTSH)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        try:
            before = read_shell_nofile_limits(process.pid)
            process.stdin.write("limit nofile 32\n")
            process.stdin.flush()

            ready, _, _ = select.select([process.stdout], [], [], 3.0)
            self.assertTrue(ready, "shell did not flush the limit command result")
            output_line = process.stdout.readline()
            self.assertIn("soft=32", output_line)

            after = read_shell_nofile_limits(process.pid)
            self.assertEqual(after, before, "RLIMIT_NOFILE of the shell process changed")
        finally:
            if process.poll() is None:
                process.stdin.write("exit\n")
                process.stdin.flush()
                process.stdin.close()
            if not process.stdin.closed:
                process.stdin.close()
            stdout_rest = process.stdout.read()
            stderr = process.stderr.read()
            returncode = process.wait(timeout=5)
            process.stdout.close()
            process.stderr.close()

        self.assertEqual(returncode, 0, msg=f"stdout={stdout_rest}\nstderr={stderr}")

    def test_invalid_nofile_configuration_is_rejected(self):
        result = run_shell("limit nofile 100 10\n/bin/echo still-alive\n")
        self.assertIn("soft limit не может быть больше hard limit", result.stderr)
        self.assertIn("still-alive", result.stdout)
        self.assertEqual(result.returncode, 0)

    def test_exit_status(self):
        result = run_shell("exit 7\n/bin/echo should-not-run\n")
        self.assertEqual(result.returncode, 7)
        self.assertNotIn("should-not-run", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
