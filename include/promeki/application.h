/**
 * @file      application.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/** Promeki Application Object
 *  You must create this object before using any promeki library calls.  It must 
 *  live for the entire lifetime you're using the promeki library.  Typically,
 *  this object would be created on the stack in the main() function so it
 *  leaves scope right before application termination.
 */
class Application {
    public:
        
        /** Returns the current application object pointer or nullptr if none */
        static Application *current();

        Application();
        ~Application();

    private:

};

PROMEKI_NAMESPACE_END


