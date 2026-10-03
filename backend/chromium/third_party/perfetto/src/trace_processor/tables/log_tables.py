# Copyright (C) 2026 Vimbrowser Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Contains the generic TrackEvent log-message table."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

from src.trace_processor.tables.metadata_tables import THREAD_TABLE


LOG_TABLE = Table(
    python_module=__file__,
    class_name="LogTable",
    # Preserve the established SQL name for consumers of Chrome TrackEvent logs.
    sql_name="__intrinsic_android_logs",
    columns=[
        C("ts", CppInt64(), cpp_access=CppAccess.READ),
        C("utid", CppTableId(THREAD_TABLE), cpp_access=CppAccess.READ),
        C("prio", CppUint32(), cpp_access=CppAccess.READ),
        C("tag", CppOptional(CppString()), cpp_access=CppAccess.READ),
        C("msg", CppString(), cpp_access=CppAccess.READ),
    ],
    tabledoc=TableDoc(
        doc="""
          Log messages emitted by generic Perfetto TrackEvent producers.

          NOTE: this table is not sorted by timestamp, so the ts column does
          not carry the sorted flag.
        """,
        group="Events",
        columns={
            "ts": "Timestamp of the log entry.",
            "utid": "Thread writing the log entry.",
            "prio": "Priority: 3=DEBUG, 4=INFO, 5=WARN, 6=ERROR.",
            "tag": "Optional source-location tag.",
            "msg": "Content of the log entry.",
        },
    ),
)

ALL_TABLES = [LOG_TABLE]
