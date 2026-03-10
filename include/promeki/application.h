/**
 * @file      application.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Promeki application lifecycle manager.
 *
 * You must create this object before using any promeki library calls. It must
 * live for the entire lifetime you're using the promeki library. Typically,
 * this object would be created on the stack in the main() function so it
 * leaves scope right before application termination.
 */
class Application {
    public:

        /**
         * @brief Returns the current application object pointer.
         * @return Pointer to the active Application, or nullptr if none exists.
         */
        static Application *current();

        /** @brief Constructs and activates the Application instance. */
        Application();

        /** @brief Destroys the Application instance and releases resources. */
        ~Application();

    private:

};

PROMEKI_NAMESPACE_END


