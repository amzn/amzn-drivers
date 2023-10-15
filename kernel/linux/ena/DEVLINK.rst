.. SPDX-License-Identifier: GPL-2.0

====================
ena devlink support
====================

This document describes the devlink features implemented by the ``ena``
device driver.

Parameters
==========

The ``ena`` driver implements the following driver-specific parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``large_llq_header``
     - Boolean
     - driverinit
     - supports toggling LLQ entry size between the default 128 bytes
       and 256 bytes.
   * - ``phc_enable``
     - Boolean
     - driverinit
     - enables/disables the PHC feature
   * - ``phc_error_bound``
     - U32
     - runtime
     - retrieve cached PHC error bound
