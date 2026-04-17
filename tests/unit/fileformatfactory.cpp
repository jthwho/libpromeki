/**
 * @file      fileformatfactory.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/fileformatfactory.h>

using namespace promeki;

// Mock product type for testing.
class MockProduct {
        public:
                enum Operation { InvalidOp = 0, Read = 1, Write = 2 };

                MockProduct() = default;
                explicit MockProduct(int val) : _value(val) {}

                bool isValid() const { return _value != 0; }
                int value() const { return _value; }

        private:
                int _value = 0;
};

using MockFactory = FileFormatFactory<MockProduct>;

// A concrete factory that handles ".mock" and ".mck" extensions.
class MockFactory_Alpha : public MockFactory {
        public:
                MockFactory_Alpha() {
                        _name = "alpha";
                        _exts = { "mock", "mck" };
                }

                bool canDoOperation(const Context &ctx) const override {
                        if(!ctx.filename.isEmpty() && isExtensionSupported(ctx.filename)) return true;
                        if(!ctx.formatHint.isEmpty() && isHintSupported(ctx.formatHint)) return true;
                        return false;
                }

                Result<MockProduct> createForOperation(const Context &ctx) const override {
                        return makeResult(MockProduct(42));
                }
};

// A second factory that handles ".other" extension.
class MockFactory_Beta : public MockFactory {
        public:
                MockFactory_Beta() {
                        _name = "beta";
                        _exts = { "other" };
                }

                bool canDoOperation(const Context &ctx) const override {
                        if(!ctx.filename.isEmpty() && isExtensionSupported(ctx.filename)) return true;
                        if(!ctx.formatHint.isEmpty() && isHintSupported(ctx.formatHint)) return true;
                        return false;
                }

                Result<MockProduct> createForOperation(const Context &ctx) const override {
                        return makeResult(MockProduct(99));
                }
};

PROMEKI_REGISTER_FILE_FORMAT_FACTORY(MockProduct, MockFactory_Alpha);
PROMEKI_REGISTER_FILE_FORMAT_FACTORY(MockProduct, MockFactory_Beta);

TEST_CASE("FileFormatFactory: lookup by filename extension") {
        MockFactory::Context ctx;
        ctx.filename = "test.mock";
        const MockFactory *f = MockFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "alpha");
}

TEST_CASE("FileFormatFactory: lookup by format hint") {
        MockFactory::Context ctx;
        ctx.formatHint = "mck";
        const MockFactory *f = MockFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "alpha");
}

TEST_CASE("FileFormatFactory: lookup finds second factory") {
        MockFactory::Context ctx;
        ctx.filename = "something.other";
        const MockFactory *f = MockFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "beta");
}

TEST_CASE("FileFormatFactory: lookup returns nullptr for unknown extension") {
        MockFactory::Context ctx;
        ctx.filename = "test.xyz";
        const MockFactory *f = MockFactory::lookup(ctx);
        CHECK(f == nullptr);
}

TEST_CASE("FileFormatFactory: lookup returns nullptr for empty context") {
        MockFactory::Context ctx;
        const MockFactory *f = MockFactory::lookup(ctx);
        CHECK(f == nullptr);
}

TEST_CASE("FileFormatFactory: createForOperation returns valid result") {
        MockFactory::Context ctx;
        ctx.filename = "test.mock";
        const MockFactory *f = MockFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        auto [product, err] = f->createForOperation(ctx);
        CHECK(err.isOk());
        CHECK(product.isValid());
        CHECK(product.value() == 42);
}

TEST_CASE("FileFormatFactory: createForOperation from second factory") {
        MockFactory::Context ctx;
        ctx.filename = "data.other";
        const MockFactory *f = MockFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        auto [product, err] = f->createForOperation(ctx);
        CHECK(err.isOk());
        CHECK(product.isValid());
        CHECK(product.value() == 99);
}

TEST_CASE("FileFormatFactory: default createForOperation returns error") {
        MockFactory f;
        MockFactory::Context ctx;
        ctx.filename = "test.mock";
        auto [product, err] = f.createForOperation(ctx);
        CHECK(err.isError());
        CHECK(err == Error::NotImplemented);
}

TEST_CASE("FileFormatFactory: default canDoOperation returns false") {
        MockFactory f;
        MockFactory::Context ctx;
        ctx.filename = "test.mock";
        CHECK_FALSE(f.canDoOperation(ctx));
}

TEST_CASE("FileFormatFactory: isExtensionSupported") {
        MockFactory_Alpha alpha;
        CHECK(alpha.isExtensionSupported("file.mock"));
        CHECK(alpha.isExtensionSupported("file.mck"));
        CHECK(alpha.isExtensionSupported("path/to/file.MOCK"));
        CHECK_FALSE(alpha.isExtensionSupported("file.wav"));
        CHECK_FALSE(alpha.isExtensionSupported("file"));
}

TEST_CASE("FileFormatFactory: isHintSupported") {
        MockFactory_Alpha alpha;
        CHECK(alpha.isHintSupported("mock"));
        CHECK(alpha.isHintSupported("MOCK"));
        CHECK(alpha.isHintSupported("mck"));
        CHECK_FALSE(alpha.isHintSupported("wav"));
        CHECK_FALSE(alpha.isHintSupported(""));
}

TEST_CASE("FileFormatFactory: extensions accessor") {
        MockFactory_Alpha alpha;
        const StringList &exts = alpha.extensions();
        CHECK(exts.size() == 2);
}

TEST_CASE("FileFormatFactory: name accessor") {
        MockFactory_Alpha alpha;
        CHECK(alpha.name() == "alpha");

        MockFactory_Beta beta;
        CHECK(beta.name() == "beta");
}

TEST_CASE("FileFormatFactory: registerFactory with nullptr returns -1") {
        int ret = MockFactory::registerFactory(nullptr);
        CHECK(ret == -1);
}
