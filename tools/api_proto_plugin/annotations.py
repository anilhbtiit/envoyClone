"""Envoy API annotations."""

from collections import namedtuple

import re

# Key-value annotation regex.
ANNOTATION_REGEX = re.compile('\[#([\w-]+?):(.*?)\]\s?', re.DOTALL)
ANNOTATION_LINE_REGEX_FORMAT = '[^\S\r\n]*\[#%s:.*?\][^\S\r\n]*\n?'

ANNOTATION_LINE_FORMAT = ' [#%s:%s]\n'

# Page/section titles with special prefixes in the proto comments
DOC_TITLE_ANNOTATION = 'protodoc-title'

# Not implemented yet annotation on leading comments, leading to hiding of
# field.
NOT_IMPLEMENTED_HIDE_ANNOTATION = 'not-implemented-hide'

# For large protos, place a comment at the top that specifies the next free field number.
NEXT_FREE_FIELD_ANNOTATION = 'next-free-field'

# Comment that allows for easy searching for things that need cleaning up in the next major
# API version.
NEXT_MAJOR_VERSION_ANNOTATION = 'next-major-version'

# Comment. Just used for adding text that will not go into the docs at all.
COMMENT_ANNOTATION = 'comment'

VALID_ANNOTATIONS = set([
    DOC_TITLE_ANNOTATION,
    NOT_IMPLEMENTED_HIDE_ANNOTATION,
    NEXT_FREE_FIELD_ANNOTATION,
    NEXT_MAJOR_VERSION_ANNOTATION,
    COMMENT_ANNOTATION,
])

VALID_ANNOTATIONS_LINE_REGEX = {
    annotation: re.compile(ANNOTATION_LINE_REGEX_FORMAT % annotation, re.DOTALL)
    for annotation in VALID_ANNOTATIONS
}

# These can propagate from file scope to message/enum scope (and be overridden).
INHERITED_ANNOTATIONS = set([
    # Nothing here right now, this used to be PROTO_STATUS_ANNOTATION. Retaining
    # this capability for potential future use.
])


class AnnotationError(Exception):
  """Base error class for the annotations module."""


def ExtractAnnotations(s, inherited_annotations=None):
  """Extract annotations map from a given comment string.

  Args:
    s: string that may contains annotations.
    inherited_annotations: annotation map from file-level inherited annotations
      (or None) if this is a file-level comment.

  Returns:
    Annotation map.
  """
  annotations = {
      k: v for k, v in (inherited_annotations or {}).items() if k in INHERITED_ANNOTATIONS
  }
  # Extract annotations.
  groups = re.findall(ANNOTATION_REGEX, s)
  for group in groups:
    annotation = group[0]
    if annotation not in VALID_ANNOTATIONS:
      raise AnnotationError('Unknown annotation: %s' % annotation)
    annotations[group[0]] = group[1].lstrip()
  return annotations


def FormatAnnotation(annotation, content):
  return ANNOTATION_LINE_FORMAT % (annotation, content)


def WithoutAnnotation(s, annotation):
  return re.sub(VALID_ANNOTATIONS_LINE_REGEX[annotation], '',
                s) if annotation in VALID_ANNOTATIONS_LINE_REGEX else s


def WithoutAnnotations(s):
  return re.sub(ANNOTATION_REGEX, '', s)
