"""Runs inside Hdf5Tests' embedded CPython, through the production nanobind module."""
import json
import pathlib
import subprocess
import sys
import tempfile
import time
import importlib.util

if importlib.util.find_spec("sst_hdf5") is None:
    import pytest
    pytest.skip("Requires the Hdf5Tests embedded Python host", allow_module_level=True)
import numpy as np
import sst_hdf5


def execute(session, request):
    started = json.loads(session.start(json.dumps(request)))
    assert started["ok"], started
    deadline = time.monotonic() + 30
    while True:
        state = json.loads(session.poll())
        if not state["running"]:
            return state["response"]
        assert time.monotonic() < deadline, state
        time.sleep(0.005)


def h5py_run(path, script):
    # h5py's wheel bundles HDF5 2.0.0 under the same DLL name as our 2.1.1.
    subprocess.run([sys._sst_hdf5_test_python, "-c",
                    "import sys,json,h5py,numpy as np\npath=sys.argv[1]\n" + script, str(path)],
                   cwd=path.parent, check=True)


with tempfile.TemporaryDirectory(prefix="sst-hdf5-interop-") as directory:
    path = pathlib.Path(directory) / "próba-测试.h5"
    session = sst_hdf5.Session()
    source = np.arange(60, dtype=float).reshape(3, 4, 5)
    save = {
        "operation": "save", "file_path": str(path),
        "provenance": {"solver": "scaps_core", "run_id": "run-α", "temperature_K": 300},
        "attributes": {"title": "Próba 测试"},
        "datasets": [{"dataset_id": "/runs/run-α/potential", "shape": list(source.shape),
                      "values": source.ravel().tolist(), "units": "V", "chunks": [1, 2, 5],
                      "attributes": {"description": "潜在"}}],
    }
    assert execute(session, save)["ok"]
    h5py_run(path, '''
with h5py.File(path, "r") as file:
        dataset = file["/runs/run-α/potential"]
        np.testing.assert_array_equal(dataset[:], np.arange(60).reshape(3,4,5))
        assert dataset.dtype == np.dtype("<f8")
        assert dataset.chunks == (1, 2, 5)
        assert dataset.compression == "gzip" and dataset.compression_opts == 4
        assert dataset.shuffle and dataset.attrs["units"] == "V"
        assert dataset.attrs["description"] == "潜在"
        assert json.loads(file.attrs["provenance_json"]) == {"solver":"scaps_core","run_id":"run-α","temperature_K":300}
        assert file.attrs["title"] == "Próba 测试"
''')
    request = {"operation": "read", "file_path": str(path), "dataset_id": "/runs/run-α/potential",
               "offset": [1, 1, 2], "count": [2, 2, 3]}
    part = execute(session, request)
    assert part["ok"] and part["shape"] == [3, 4, 5] and part["units"] == "V", part
    np.testing.assert_array_equal(np.array(part["values"]).reshape(part["count"]), source[1:3, 1:3, 2:5])
    assert part["attributes"] == {"description": "潜在"}
    original = path.read_bytes()
    # The child exits inside save, after writing a dataset, without C++ stack unwinding.
    interrupted = subprocess.run([sys._sst_hdf5_test_host, "--interrupt-save", str(path)], check=False)
    assert interrupted.returncode == 73, interrupted.returncode
    assert path.read_bytes() == original
    h5py_run(path, '''
with h5py.File(path, "r") as file:
    np.testing.assert_array_equal(file["/runs/run-α/potential"][:], np.arange(60).reshape(3,4,5))
''')
    assert list(path.parent.glob(path.name + ".tmp-*")), "Expected abandoned crash temporary"
    for temporary in path.parent.glob(path.name + ".tmp-*"):
        temporary.unlink()
    invalid = dict(request, count=[-1, 2, 3])
    assert execute(session, invalid)["error"]["code"] == "hdf5.invalid_request"
    invalid = dict(request, count=[100, 2, 3])
    assert execute(session, invalid)["error"]["code"] == "hdf5.invalid_request"
    invalid = dict(request, dataset_id="/missing")
    assert execute(session, invalid)["error"]["code"] == "hdf5.format"
    assert not json.loads(session.start('{"operation":"delete"}'))["ok"]
    assert json.loads(session.start("{"))["error"]["code"] == "hdf5.invalid_request"
    assert path.read_bytes() == original
    # Reverse interoperability: fixed/vlen UTF-8 metadata and big-endian doubles from h5py.
    external = path.parent / "external.h5"
    h5py_run(external, '''
with h5py.File(path, "w") as file:
        dataset = file.create_dataset("density", data=np.arange(12, dtype=">f8").reshape(3, 4))
        dataset.attrs["units"] = "cm^-3"
        dataset.attrs["fixed_note"] = np.bytes_("fixed")
        file.attrs["provenance_json"] = '{"source":"h5py"}'
''')
    result = execute(session, {"operation": "read", "file_path": str(external), "dataset_id": "/density"})
    assert result["ok"] and result["values"] == list(range(12)), result
    assert result["attributes"] == {"fixed_note": "fixed"}
    assert result["file_attributes"]["provenance_json"] == '{"source":"h5py"}'
    print("h5py interop: round trip, hyperslab, Unicode, compression, provenance, crash safety passed")
