from distutils.core import setup
setup(name='STOCKspendfrom',
      version='1.0',
      description='Command-line utility for stock "coin control"',
      author='Gavin Andresen',
      author_email='gavin@stockfoundation.org',
      requires=['jsonrpc'],
      scripts=['spendfrom.py'],
      )
