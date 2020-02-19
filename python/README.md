# Midas python tools

This directory contains midas tools written in python.

* `/midas` contains the main code. Note that most of the tools support python 3 only.
* `/examples` contains some complete examples of clients and frontends.
* `/tests` contains test suites that interact with your experiment. You can run
  them using `cd $MIDASSYS/python/tests; python -munittest`

## The tools

### Midas file reader

`midas/file_reader.py` is a pure-python tool that lets you read midas files.
It presents a very pythonic interface, allowing you to do things like
`for event in my_file: event.dump()`.

See the examples in `midas/file_reader.py` and `examples/file_reader.py`.

* It works with python 2.7 and python 3.
* It does not require the rest of midas to be compiled.

### Client and frontend

**THESE TOOLS REQUIRE MIDAS TO BE BUILT USING CMAKE - SEE MORE BELOW!**

`midas/client.py` provides pythonic access to a midas experiment, by wrapping 
midas' C library. It provides nice ODB access through functions like `odb_set`
and `odb_get`, and can also handle event buffers, callback functions and more.

`midas/frontend.py` builds upon the client and provides a framework for writing
midas frontends in python. It supports periodic and polled equipment, and can
be controlled just like a C frontend.

Many users write scripts in python or bash that call the `odbedit` command-line
tool or the mhttpd web server. We hope that the pythonic midas client tools
will simplify and robustify such scripts.

The python frontend framework may prove particularly beneficial for controlling
devices that natively talk in JSON or other text-based protocols that are
tedious to deal with in C. It may also reduce the development time required to
write frontends that do not have strict performance requirements.

* They work with python 3 only.
* They require the rest of midas to be compiled, as they use the midas C library.
  For the correct libraries to be built, you must compile using CMake 
  (e.g. `cd $MIDASSYS; mkdir build; cd build; cmake3 ..; make; make install`)
* They are designed for ease of use, rather than optimal performance. If
  performance is critical for your application, you should write your code in C.

## Documentation

Documentation of the tools is written as docstrings in the source code. We do
not currently have a web version of the documentation. 

The documentation assumes a reasonable familiarity with core midas concepts.
See the [Midas Wiki](https://midas.triumf.ca/MidasWiki/index.php/Main_Page) if
you're unfamiliar with a term or concept in the python documentation. 

## Installation

We highly recommed using `pip` and/or `virtualenv` to manage your python packages, but you can also just edit your paths if desired.

### Installing by editing paths

If you don't want to use `pip` (see below), you can edit your environment 
variables so python knows where to find the midas module.

You can either do this in your shell, before you run `python`:

```bash
export PYTHONPATH=$PYTHONPATH:$MIDASSYS/python
```

Or you can do it from within your python program itself:

```python
import sys
sys.path.append("/path/to/midas/python")
import midas
```

### Installing using pip

`pip` allows you to install packages from online repositories (e.g. 
`pip install lz4` will download and install the `lz4` package from the PyPi 
online repository), and also allows you to install packages you already have 
on disk.

`virtualenv` allows you to set up multiple virtual python environments, so you 
can use, for example, one set of packages when working on experiment A, and a 
second set of packages when working on experiment B. Without virtualenv you can 
quickly get into "dependency hell" where different experiments have conflicting 
requirements.

To install pip (suggested):

```bash
wget https://bootstrap.pypa.io/get-pip.py
python get-pip.py --user
```

To install virtualenv (recommended; optional; requires pip):

```bash
pip install virtualenv --user
pip install virtualenvwrapper --user
source ~/.local/bin/virtualenvwrapper.sh # Also add this to your .bashrc or similar
mkvirtualenv DEMO # You can call your environment anything you want

# You are now in your virtual python environment. 
# You can leave it by running `deactivate`.
# You can re-enter it by running `workon DEMO` (or whatever you called your venv).
# You can create multiple virtualenvs, and even specify different python versions for each.
```

To install this package with pip:

```bash
pip install -e $MIDASSYS/python --user
# The -e flag makes this an "editable" install. If you upgrade midas in future,
# python will automatically see the new code without you having to re-install.
```

You can now use `import midas` and `import midas.file_reader` etc in your python scripts.

## Running tests

Test are written using the python `unittest` framework. To run all the tests, change to
the `tests` directory and run `python -m unittest`. To run a single script, run e.g.
`python test_odb.py`. Note that the tests will interact with whichever experiment you
are connected to, and will start and stop runs, interact with the ODB etc.
