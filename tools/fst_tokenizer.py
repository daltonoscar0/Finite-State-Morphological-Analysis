#!/usr/bin/env python3
"""Client for the analyzer's surface segmentation mode.

The analyzer loads its lexicon on every start, which costs far more than
analyzing a word, so this keeps one `analyzer --segment-jsonl` process alive
and talks to it over pipes. The protocol is strictly one line in, one JSON
record out, in order.

    with FstTokenizer("./analyzer") as tok:
        print(tok.encode("cities"))        # ['cit', 'ies']
        print(tok.segment("stopped"))      # {'word': 'stopped', ...}
        print(tok.decode(['cit', 'ies']))  # 'cities'

Standard library only.
"""

import json
import os
import subprocess
import tempfile


class AnalyzerError(RuntimeError):
    """The analyzer process failed, died, or produced unreadable output."""


class FstTokenizer:
    """A long-lived `analyzer --segment-jsonl` process.

    Attributes:
        nbest: How many analyses the analyzer reports per word. With nbest > 1
            each record carries an "alternatives" list.
    """

    # Words are sent in chunks, and each chunk is fully read back before the
    # next is sent. The chunk size is bounded so that a chunk's worth of
    # output can never fill the pipe buffer while this process is still
    # writing, which would deadlock both sides. The bound is on estimated
    # output bytes, not just on the number of words, because one very long
    # word can produce a very long record.
    MAX_CHUNK_WORDS = 128
    MAX_CHUNK_OUTPUT_BYTES = 16 * 1024

    def __init__(self, analyzer="./analyzer", nbest=1, cwd=None):
        """Start the analyzer.

        Args:
            analyzer: Path to the analyzer binary.
            nbest: Value for --nbest.
            cwd: Working directory for the child. Defaults to the directory
                holding the binary, because the analyzer loads
                `data/english_lexicon.tsv` by a relative path.

        Raises:
            AnalyzerError: If the binary is missing or will not start.
        """
        self.nbest = int(nbest)
        if self.nbest < 1:
            raise ValueError("nbest must be at least 1")

        analyzer_path = os.path.abspath(analyzer)
        if not os.path.isfile(analyzer_path):
            raise AnalyzerError("analyzer not found: %s" % analyzer_path)
        if not os.access(analyzer_path, os.X_OK):
            raise AnalyzerError("analyzer is not executable: %s" % analyzer_path)

        if cwd is None:
            cwd = os.path.dirname(analyzer_path) or "."

        argv = [analyzer_path, "--segment-jsonl"]
        if self.nbest > 1:
            argv += ["--nbest", str(self.nbest)]

        # The child writes startup notices to stderr. Those go to a temporary
        # file rather than a pipe: nothing here reads stderr during normal
        # operation, and an unread pipe would eventually block the child.
        self._stderr = tempfile.TemporaryFile()

        try:
            self._proc = subprocess.Popen(
                argv,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=self._stderr,
                cwd=cwd,
            )
        except OSError as exc:
            self._stderr.close()
            raise AnalyzerError("could not start %s: %s" % (analyzer_path, exc))

        self._closed = False

    # ── Process state ─────────────────────────────────────────────────────

    def _stderr_tail(self, limit=2000):
        """Return the tail of the child's stderr, for error messages."""
        try:
            self._stderr.flush()
            self._stderr.seek(0)
            data = self._stderr.read()
        except (OSError, ValueError):
            return ""
        text = data.decode("utf-8", "replace") if isinstance(data, bytes) else data
        return text[-limit:].strip()

    def _die(self, detail):
        """Raise an AnalyzerError describing a dead or broken child."""
        message = detail
        code = self._proc.poll()
        if code is not None:
            message += "; analyzer exited with status %d" % code
        tail = self._stderr_tail()
        if tail:
            message += "\nanalyzer stderr:\n%s" % tail
        raise AnalyzerError(message)

    def _check_alive(self):
        if self._closed:
            raise AnalyzerError("tokenizer is closed")
        if self._proc.poll() is not None:
            self._die("analyzer process is no longer running")

    # ── Core protocol ─────────────────────────────────────────────────────

    @staticmethod
    def _clean(word):
        """Flatten a word to something the one-word-per-line protocol allows.

        Newlines would be read as record separators and a carriage return is
        stripped by the analyzer, so neither can appear in a word. They are
        removed here rather than silently corrupting the stream.
        """
        if not isinstance(word, str):
            raise TypeError("words must be str, got %r" % type(word).__name__)
        return word.replace("\r", "").replace("\n", "")

    def _write_chunk(self, words):
        """Send one chunk of words, then read back exactly that many records."""
        payload = "".join(w + "\n" for w in words).encode("utf-8", "surrogateescape")

        try:
            self._proc.stdin.write(payload)
            self._proc.stdin.flush()
        except (BrokenPipeError, OSError) as exc:
            self._die("failed writing to analyzer: %s" % exc)

        records = []
        for word in words:
            line = self._proc.stdout.readline()
            if not line:
                self._die("analyzer closed its output after %d of %d records "
                          "in this batch (last word sent: %r)"
                          % (len(records), len(words), word))
            text = line.decode("utf-8", "surrogateescape").rstrip("\n")
            try:
                records.append(json.loads(text))
            except ValueError as exc:
                raise AnalyzerError(
                    "analyzer emitted unparseable JSON for %r: %s\nline: %s"
                    % (word, exc, text[:400])
                )
        return records

    def _chunks(self, words):
        """Split words into chunks small enough to never fill the pipe."""
        chunk, estimate = [], 0
        for word in words:
            # Worst case a byte is escaped as \u00XX (six bytes) and appears
            # both in "word" and across the pieces, times nbest for the
            # alternatives, plus a fixed allowance for the other fields.
            cost = 200 + 12 * len(word.encode("utf-8", "surrogateescape"))
            cost *= self.nbest
            if chunk and (len(chunk) >= self.MAX_CHUNK_WORDS or
                          estimate + cost > self.MAX_CHUNK_OUTPUT_BYTES):
                yield chunk
                chunk, estimate = [], 0
            chunk.append(word)
            estimate += cost
        if chunk:
            yield chunk

    # ── Public interface ──────────────────────────────────────────────────

    def segment(self, word):
        """Return the record for one word.

        Args:
            word: The word to segment. May be empty.

        Returns:
            The parsed JSON record as a dict.

        Raises:
            AnalyzerError: If the analyzer died or produced bad output.
        """
        return self.segment_many([word])[0]

    def segment_many(self, words):
        """Return one record per word, in the order given.

        Accepts any iterable, including a generator, and handles batches of
        any size without deadlocking.
        """
        self._check_alive()
        cleaned = [self._clean(w) for w in words]
        records = []
        for chunk in self._chunks(cleaned):
            records.extend(self._write_chunk(chunk))
        return records

    def encode(self, word):
        """Return just the surface pieces for one word."""
        return self.segment(word)["pieces"]

    @staticmethod
    def decode(pieces):
        """Rebuild a word from its pieces.

        The pieces are substrings of the original input, so this is a plain
        concatenation and is the exact inverse of encode().
        """
        return "".join(pieces)

    # ── Lifecycle ─────────────────────────────────────────────────────────

    def close(self):
        """Shut the analyzer down and release its pipes."""
        if self._closed:
            return
        self._closed = True

        proc = self._proc
        try:
            if proc.stdin and not proc.stdin.closed:
                try:
                    proc.stdin.close()  # EOF on stdin ends the read loop
                except OSError:
                    pass
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        finally:
            if proc.stdout and not proc.stdout.closed:
                try:
                    proc.stdout.close()
                except OSError:
                    pass
            try:
                self._stderr.close()
            except OSError:
                pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def __del__(self):
        # Best effort only: interpreter shutdown may already have torn down
        # the modules this needs.
        try:
            self.close()
        except Exception:
            pass


# ── Manual check ──────────────────────────────────────────────────────────

def _main(argv):
    """Segment words given as arguments, or stdin lines if there are none."""
    import sys

    analyzer = os.environ.get("ANALYZER", "./analyzer")
    words = argv[1:]
    if not words:
        words = [line.rstrip("\n") for line in sys.stdin]

    with FstTokenizer(analyzer) as tok:
        for record in tok.segment_many(words):
            ok = tok.decode(record["pieces"]) == record["word"]
            print("%-20s %-28s %-14s round-trip=%s" % (
                record["word"],
                "|".join(record["pieces"]),
                record["gate"],
                "ok" if ok else "FAILED",
            ))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(_main(sys.argv))
