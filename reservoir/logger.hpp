/*******************************************************************************
 * reservoir/logger.hpp
 *
 * Logger utils
 *
 * Copyright (C) 2019 Lorenz Hübschle-Schneider <lorenz@4z2.de>
 *
 * All rights reserved. Published under the GNU General Public License 3
 ******************************************************************************/

#pragma once
#ifndef RESERVOIR_LOGGER_HEADER
#define RESERVOIR_LOGGER_HEADER

#include <tlx/logger.hpp>

#define LOGRC(cond)  LOGC((cond) && comm_.rank() == 0) << short_name << ' '
#define sLOGRC(cond) sLOGC((cond) && comm_.rank() == 0) << short_name

#define LOGR  LOGRC(debug)
#define sLOGR sLOGRC(debug)

#define LOGR1  LOGRC(true)
#define sLOGR1 sLOGRC(true)

#define LOGR0  LOGRC(false)
#define sLOGR0 sLOGRC(false)

#define pLOGC(cond)  LOGC(cond) << short_name << " PE " << comm_.rank() << ' '
#define spLOGC(cond) sLOGC(cond) << short_name << "PE" << comm_.rank()

#define pLOG  pLOGC(debug)
#define spLOG spLOGC(debug)

#define pLOG1  pLOGC(true)
#define spLOG1 spLOGC(true)
#define pLOG0  pLOGC(false)
#define spLOG0 spLOGC(false)

#endif // RESERVOIR_LOGGER_HEADER
